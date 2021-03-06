#include "GitDiff.h"

// CommitData

struct GitDiff::CommitData {
	enum Type {
		UNKNOWN,
		TREE,
		BLOB,
	};
	Type type = UNKNOWN;
	QString path;
	QString id;
	QString mode;
	QString parent;
};

// IndexMap

class GitDiff::IndexMap {
private:
	std::map<QString, QString> map;
public:

	typedef std::map<QString, QString>::const_iterator const_iterator;

	void store(QString const &path, QString const &index)
	{
		map[path] = index;
	}

	void store(QList<CommitData> const &files)
	{
		for (CommitData const &cd : files) {
			store(cd.path, cd.id);
		}
	}

	const_iterator find(QString const &path) const
	{
		return map.find(path);
	}

	const_iterator end() const
	{
		return map.end();
	}
};

// CommitList

class GitDiff::CommitList {
public:
	QList<CommitData> files;
	QStringList parents;

	bool parseTree(Git *g, QString const &index, QString const &dir, bool append);
	bool parseCommit(Git *g, QString const &index, bool append);
};

bool GitDiff::CommitList::parseTree(Git *g, const QString &index, const QString &dir, bool append)
{
	if (!append) {
		files.clear();
	}
	if (g && !index.isEmpty()) {
		QByteArray ba;
		if (g->cat_file(index, &ba)) {
			QString s = QString::fromUtf8(ba);
			QStringList lines = misc::splitLines(s);
			for (QString const &line : lines) {
				int tab = line.indexOf('\t');
				if (tab > 0) {
					QString stat = line.mid(0, tab);
					QStringList vals = misc::splitWords(stat);
					if (vals.size() >= 3) {
						CommitData data;
						data.mode = vals[0];
						data.id = vals[2];
						QString type = vals[1];
						QString path = line.mid(tab + 1);
                        path = Git::trimPath(path);
						data.path = misc::joinWithSlash(dir, path);
						if (type == "tree") {
							data.type = CommitData::TREE;
							files.push_back(data);
						} else if (type == "blob") {
							data.type = CommitData::BLOB;
							files.push_back(data);
						}
					}
				}
			}
			return true;
		}
	}
	return false;
}

bool GitDiff::CommitList::parseCommit(Git *g, const QString &index, bool append)
{
	if (!append) {
		files.clear();
		parents.clear();
	}
	if (g && !index.isEmpty()) {
		QString tree;
		QStringList parents;
		{
			QByteArray ba;
			if (g->cat_file(index, &ba)) {
				QStringList lines = misc::splitLines(QString::fromUtf8(ba));
				for (QString const &line : lines) {
					int i = line.indexOf(' ');
					if (i < 1) break;
					QString key = line.mid(0, i);
					QString val = line.mid(i + 1).trimmed();
					if (key == "tree") {
						tree = val;
					} else if (key == "parent") {
						parents.push_back(val);
					}
				}
			}
		}
		if (!tree.isEmpty()) {
			CommitList commit;
			if (commit.parseTree(g, tree, QString(), true)) {
				this->files.append(commit.files);
				this->parents.append(parents);
				return true;
			}
		}
	}
	return false;
}

// GitDiff

void GitDiff::diff_tree_(Git *g, const QString &dir, QString older_index, QString newer_index)
{
	CommitList older_commit;
	CommitList newer_commit;
	older_commit.parseTree(g, older_index, dir, false);
	newer_commit.parseTree(g, newer_index, dir, false);

	MapList diffmap;

	diffmap.push_back(IndexMap());
	IndexMap &map = diffmap.front();
	for (CommitData const &cd : older_commit.files) {
		map.store(cd.path, cd.id);
	}

	commit_into_map(g, dir, newer_commit, &diffmap);
}

void GitDiff::commit_into_map(Git *g, const QString &dir, const GitDiff::CommitList &commit, const GitDiff::MapList *diffmap)
{
	Git::Diff item;
	auto AddItem = [&](){
		item.diff = QString("diff --git ") + item.blob.a.path + ' ' + item.blob.b.path;
		item.index = QString("index ") + item.blob.a.id + ".." + item.blob.b.id + ' ' + item.mode;
		diffs.push_back(item);
	};
	for (CommitData const &cd : commit.files) {
		for (IndexMap const &map : *diffmap) {
			auto it = map.find(cd.path);
			if (it == map.end()) {
				item = Git::Diff();
				item.path = cd.path;
				item.mode = cd.mode;
				item.blob.b.id = cd.id;
				item.blob.a.path = cd.path;
				item.blob.b.path = cd.path;
				AddItem();
			} else {
				if (cd.id != it->second) {
					if (cd.type == CommitData::TREE) {
						QString path = misc::joinWithSlash(dir, cd.path);
						diff_tree_(g, path, it->second, cd.id);
					} else if (cd.type == CommitData::BLOB) {
						item = Git::Diff();
						item.path = cd.path;
						item.mode = cd.mode;
						item.blob.a.id = it->second;
						item.blob.b.id = cd.id;
						item.blob.a.path = cd.path;
						item.blob.b.path = cd.path;
						AddItem();
					}
					break;
				}
			}
		}
	}
}

void GitDiff::file_into_map(const Git::FileStatusList &stats, const GitDiff::MapList *diffmap)
{
	Git::Diff item;
	auto AddItem = [&](Git::FileStatus const &st){
		item.diff = QString("diff --git ") + item.blob.a.path + ' ' + item.blob.b.path;
		item.index = QString("index ") + item.blob.a.id + ".." + item.blob.b.id + ' ' + item.mode;
		item.path = st.path1();
		item.blob.a.path = misc::joinWithSlash("a", item.path); // a/path
		item.blob.b.path = misc::joinWithSlash("b", item.path); // b/path
		diffs.push_back(item);
		item = Git::Diff();
	};
	for (Git::FileStatus const &st : stats) {
		bool done = false;
		for (IndexMap const &map : *diffmap) {
			auto it = map.find(st.path1());
			if (it != map.end()) {
				item.blob.a.id = it->second;
				AddItem(st);
				done = true;
				break;
			}
		}
		if (!done) {
			AddItem(st);
		}
	}
}

void GitDiff::parse_commit(Git *g, const QString &dir, const QString &index, GitDiff::MapList *diffmaplist)
{
	CommitList oldcommit;
	oldcommit.parseCommit(g, index, true);
	diffmaplist->push_back(IndexMap());
	IndexMap &map = diffmaplist->front();
	for (CommitData const &cd : oldcommit.files) {
		QString path = misc::joinWithSlash(dir, cd.path);
		map.store(path, cd.id);
	}
}

void GitDiff::parse_tree(Git *g, const QString &dir, const QString &index, std::set<QString> *dirset, GitDiff::MapList *diffmaplist)
{
	if (!dir.isEmpty()) {
		auto it = dirset->find(dir);
		if (it != dirset->end()) {
			return;
		}
		dirset->insert(dir);
	}

	CommitList commit;
	commit.parseTree(g, index, dir, false);
	diffmaplist->push_back(IndexMap());
	IndexMap &map = diffmaplist->front();
	for (CommitData const &cd : commit.files) {
		map.store(cd.path, cd.id);
	}
}

void GitDiff::diff(Git *g, QString index, QList<Git::Diff> *out)
{
	if (!g) return;

	out->clear();

	if (index == "HEAD") {

		QString parent = g->revparseHEAD(); // HEADのインデックスを取得

		Git::FileStatusList stats = g->status(); // git status

		MapList diffmaplist;

		parse_commit(g, QString(), parent, &diffmaplist);

		std::set<QString> dirset;

		for (Git::FileStatus const &s : stats) {
			QStringList list = s.path1().split('/');
			QString path;
			for (int i = 0; i + 1 < list.size(); i++) {
				QString const &s = list[i];
				path = misc::joinWithSlash(path, s);
				for (IndexMap const &map : diffmaplist) {
					auto it = map.find(path);
					if (it != map.end()) {
						QString index = it->second;
						parse_tree(g, path, index, &dirset, &diffmaplist);
					}
				}

			}
		}

		file_into_map(stats, &diffmaplist);

	} else {

		CommitList newcommit;
		newcommit.parseCommit(g, index, false);

		MapList diffmaplist;

		CommitList oldcommit;
		for (QString parent : newcommit.parents) {
			oldcommit.parseCommit(g, parent, true);
			diffmaplist.push_back(IndexMap());
			IndexMap &map = diffmaplist.front();
			for (CommitData const &cd : oldcommit.files) {
				map.store(cd.path, cd.id);
			}
		}

		commit_into_map(g, QString(), newcommit, &diffmaplist);
	}

	std::sort(diffs.begin(), diffs.end(), [](Git::Diff const &left, Git::Diff const &right){
		return left.blob.b.path.compare(right.blob.b.path, Qt::CaseInsensitive) < 0;
	});

	//

	for (Git::Diff const &file : diffs) {
		QString s;
		if (!file.blob.a.id.isEmpty() && !file.blob.b.id.isEmpty()) {
			s = g->diff(file.blob.a.id, file.blob.b.id);
		} else {
			s = g->diff_to_file(file.blob.a.id, file.path);
		}
		QStringList lines = misc::splitLines(s);

		Git::Diff diff;

		diff.diff = QString("diff --git ") + file.blob.a.path + ' ' + file.blob.b.path;
		diff.index = QString("index ") + file.blob.a.id + ".." + file.blob.b.id + ' ' + file.mode;
		diff.blob = file.blob;

		bool f = false;
		for (QString const &line : lines) {
			ushort c = line.utf16()[0];
			if (c == '@') {
				if (line.startsWith("@@ ")) {
					diff.hunks.push_back(Git::Hunk());
					diff.hunks.back().at = line;
					f = true;
				}
			} else {
				if (f) {
					if (c == ' ' || c == '-' || c == '+') {
						// nop
					} else {
						f = false;
					}
				}
				if (f) {
					if (!diff.hunks.isEmpty()) {
						diff.hunks.back().lines.push_back(line);
					}
				}
			}
		}

		out->push_back(diff);
	}
}

