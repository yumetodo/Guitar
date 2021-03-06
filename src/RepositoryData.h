#ifndef REPOSITORYDATA_H
#define REPOSITORYDATA_H

#include <QList>

struct RepositoryItem {
	QString name;
	QString local_dir;
};

class RepositoryBookmark {
public:
	RepositoryBookmark();
	static bool save(QString const &path, QList<RepositoryItem> const *items);
	static QList<RepositoryItem> load(QString const &path);
};

#endif // REPOSITORYDATA_H
