#ifndef QMLUPDATEINFOMODEL_H
#define QMLUPDATEINFOMODEL_H

#include <QtCore/QAbstractListModel>

#include <QtAutoUpdaterCore/UpdateInfo>

class QmlUpdateInfoModel : public QAbstractListModel
{
	Q_OBJECT

	Q_PROPERTY(QList<QtAutoUpdater::UpdateInfo> updateInfos MEMBER _updateInfos WRITE setUpdateInfos NOTIFY updateInfosChanged)

public:
	enum Roles {
		NameRole = Qt::UserRole,
		VersionRole,
		SizeRole,
		IdentifierRole,
		GadgetRole
	};
	Q_ENUM(Roles)

	explicit QmlUpdateInfoModel(QObject *parent = nullptr);

	// Basic functionality:
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
	void updateInfosChanged(const QList<QtAutoUpdater::UpdateInfo> &updateInfos);

private:
	QList<QtAutoUpdater::UpdateInfo> _updateInfos;

	void setUpdateInfos(QList<QtAutoUpdater::UpdateInfo> updateInfos);
};

#endif // QMLUPDATEINFOMODEL_H