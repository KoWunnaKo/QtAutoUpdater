#include "updatecontroller.h"
#include "updatecontroller_p.h"
#include "updatebutton.h"
#include "installwizard_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <dialogmaster.h>

#include <QtAutoUpdaterCore/private/updater_p.h>

using namespace QtAutoUpdater;

UpdateController::UpdateController(QObject *parent) :
	UpdateController{nullptr, parent}
{}

UpdateController::UpdateController(QWidget *parentWindow) :
	UpdateController{nullptr, parentWindow}
{}

UpdateController::UpdateController(Updater *updater, QObject *parent) :
	QObject{*new UpdateControllerPrivate{}, parent}
{
	setUpdater(updater);
}

UpdateController::UpdateController(Updater *updater, QWidget *parentWidget) :
	QObject{*new UpdateControllerPrivate{}, parentWidget}
{
	setUpdater(updater);
}

UpdateController::~UpdateController()
{
	Q_D(UpdateController);
	if(d->running)
		qCWarning(logQtAutoUpdater) << "UpdaterController destroyed while still running! This can crash your application!";

	d->hideProgress();
}

QAction *UpdateController::createUpdateAction(Updater *updater, QObject *parent)
{
	auto updateAction = new QAction {
		UpdateControllerPrivate::getUpdatesIcon(),
		tr("Check for Updates"),
		parent
	};
	updateAction->setMenuRole(QAction::ApplicationSpecificRole);
	updateAction->setToolTip(tr("Checks if new updates are available. You will be prompted before updates are installed."));

	connect(updateAction, &QAction::triggered,
			updater, &Updater::checkForUpdates);
	connect(updater, &Updater::runningChanged,
			updateAction, &QAction::setDisabled);
	connect(updater, &Updater::destroyed,
			updateAction, std::bind(&QAction::setDisabled, updateAction, true));

	return updateAction;
}

QWidget *UpdateController::parentWindow() const
{
	return qobject_cast<QWidget*>(parent());
}

bool UpdateController::isRunning() const
{
	Q_D(const UpdateController);
	return d->running;
}

UpdateController::DisplayLevel UpdateController::displayLevel() const
{
	Q_D(const UpdateController);
	return d->displayLevel;
}

QString UpdateController::desktopFileName() const
{
	Q_D(const UpdateController);
	return d->desktopFileName;
}

Updater *UpdateController::updater() const
{
	Q_D(const UpdateController);
	return d->updater;
}

void UpdateController::setDisplayLevel(UpdateController::DisplayLevel displayLevel)
{
	Q_D(UpdateController);
	if (d->displayLevel == displayLevel)
		return;

	d->displayLevel = displayLevel;
	emit displayLevelChanged(d->displayLevel, {});
}

void UpdateController::setDesktopFileName(QString desktopFileName)
{
	Q_D(UpdateController);
	if (d->desktopFileName == desktopFileName)
		return;

	d->desktopFileName = std::move(desktopFileName);
	emit desktopFileNameChanged(d->desktopFileName, {});
}

void UpdateController::setUpdater(Updater *updater)
{
	Q_D(UpdateController);
	if (d->updater == updater)
		return;

	// cleanup old one
	if (d->updater) {
		d->updater->disconnect(this);
		if (d->updater->parent() == this)
			d->updater->deleteLater();
	}

	// setup new one
	d->updater = updater;
	if (d->updater) {
		QObjectPrivate::connect(d->updater, &Updater::stateChanged,
								d, &UpdateControllerPrivate::_q_updaterStateChanged,
								Qt::QueuedConnection);
		QObjectPrivate::connect(d->updater, &Updater::showInstaller,
								d, &UpdateControllerPrivate::_q_showInstaller);
		QObjectPrivate::connect(d->updater, &Updater::destroyed,
								d, &UpdateControllerPrivate::_q_updaterDestroyed);
		d->_q_updaterStateChanged(d->updater->state());
	}
	emit updaterChanged(d->updater, {});
}

bool UpdateController::start()
{
	Q_D(UpdateController);
	if(d->running || !d->updater)
		return false;

	d->ensureRunning(true);

	// ask if updates should be checked
	if(d->displayLevel >= AskLevel) {
		if(DialogMaster::questionT(parentWindow(),
								   tr("Check for Updates"),
								   tr("Do you want to check for updates now?"))
		   != QMessageBox::Yes) {
			d->ensureRunning(false);
			return false;
		}
	}

	// check for updates
	d->updater->checkForUpdates();
	return true;
}

bool UpdateController::start(DisplayLevel displayLevel)
{
	setDisplayLevel(displayLevel);
	return start();
}

bool UpdateController::cancelUpdate(int maxDelay)
{
	Q_D(UpdateController);  // TODO remove
	if(d->updater && d->updater->state() == Updater::State::Checking) {
		d->wasCanceled = true;
		if(d->checkUpdatesProgress)
			d->checkUpdatesProgress->setCanceled();
		d->updater->abortUpdateCheck(maxDelay);
		return true;
	} else
		return false;
}

//----------------- private implementation -----------------

QIcon UpdateControllerPrivate::getUpdatesIcon()
{
	const auto altPath = qEnvironmentVariable("QT_AUTOUPDATER_UPDATE_ICON",
											  QStringLiteral(":/QtAutoUpdater/icons/update.ico"));
	return QIcon::fromTheme(QStringLiteral("system-software-update"), QIcon{altPath});
}

void UpdateControllerPrivate::_q_updaterStateChanged(Updater::State state)
{
	// is possible, because queued connection
	if (!updater)
		return;

	switch (state) {
	case Updater::State::NoUpdates:
		enterNoUpdatesState();
		break;
	case Updater::State::Checking:
		enterCheckingState();
		break;
	case Updater::State::NewUpdates:
		enterNewUpdatesState();
		break;
	case Updater::State::Error:
		enterErrorState();
		break;
	case Updater::State::Installing:
		enterInstallingState();
		break;
	}
}

void UpdateControllerPrivate::_q_showInstaller(UpdateInstaller *installer)
{
	auto wizard = new InstallWizard{installer};
	wizard->show();
	wizard->raise();
	wizard->activateWindow();
}

void UpdateControllerPrivate::_q_updaterDestroyed()
{
	Q_Q(UpdateController);
	hideProgress();
	ensureRunning(false);
	emit q->updaterChanged(nullptr, {});
}

void UpdateControllerPrivate::enterNoUpdatesState()
{
	Q_Q(UpdateController);
	hideProgress();
	if (showCanceled())
		return;

	if(running && displayLevel >= UpdateController::ExtendedInfoLevel) {
		DialogMaster::informationT(q->parentWindow(),
								   UpdateController::tr("Check for Updates"),
								   UpdateController::tr("No new updates available!"));
	}
	ensureRunning(false);
}

void UpdateControllerPrivate::enterCheckingState()
{
	Q_Q(UpdateController);
	ensureRunning(true);
	if(displayLevel >= UpdateController::ProgressLevel && !checkUpdatesProgress) {
		checkUpdatesProgress = new ProgressDialog{desktopFileName, q->parentWindow()};
		QObject::connect(checkUpdatesProgress.data(), &ProgressDialog::canceled, q_func(), [this](){
			wasCanceled = true;
		});
		checkUpdatesProgress->open(updater);
	}
}

void UpdateControllerPrivate::enterNewUpdatesState()
{
	Q_Q(UpdateController);
	ensureRunning(true);
	hideProgress();
	if (showCanceled())
		return;

	if(displayLevel >= UpdateController::InfoLevel) {
		const auto updateInfos = updater->updateInfo();
		const auto res = UpdateInfoDialog::showUpdateInfo(updateInfos,
														  desktopFileName,
														  updater->backend()->features(),
														  q->parentWindow());

		switch(res) {
		case UpdateInfoDialog::InstallNow:
			updater->runUpdater(false);
			if (updater->willRunOnExit())
				qApp->quit();
			break;
		case UpdateInfoDialog::InstallLater:
			updater->runUpdater(true);
			break;
		case UpdateInfoDialog::NoInstall:
			break;
		default:
			Q_UNREACHABLE();
		}
	} else {
		updater->runUpdater(false);
		if (updater->willRunOnExit()) {
			if(displayLevel >= UpdateController::ExitLevel) {
				DialogMaster::informationT(q->parentWindow(),
										   UpdateController::tr("Install Updates"),
										   UpdateController::tr("New updates are available. The update tool will be "
											  "started to install those as soon as you close the application!"));
			} else
				qApp->quit();
		}
	}
	ensureRunning(false);
}

void UpdateControllerPrivate::enterErrorState()
{
	Q_Q(UpdateController);
	ensureRunning(true);
	hideProgress();
	if (showCanceled())
		return;

	if(displayLevel >= UpdateController::ExtendedInfoLevel) {
		DialogMaster::criticalT(q->parentWindow(),
								UpdateController::tr("Check for Updates"),
								UpdateController::tr("An error occured while trying to check for updates!"));
	}
	ensureRunning(false);
}

void UpdateControllerPrivate::enterInstallingState()
{
	// nothing for now
}

void UpdateControllerPrivate::ensureRunning(bool newState)
{
	Q_Q(UpdateController);
	if (running != newState) {
		running = newState;
		if (!running)
			wasCanceled = false;
		emit q->runningChanged(running, {});
	}
}

void UpdateControllerPrivate::hideProgress()
{
	if(checkUpdatesProgress) {
		checkUpdatesProgress->hide();  // explicitly hide so child dialogs are NOT shown on top of it
		checkUpdatesProgress->deleteLater();
		checkUpdatesProgress.clear();
	}
}

bool UpdateControllerPrivate::showCanceled()
{
	Q_Q(UpdateController);
	if(wasCanceled) {
		if(displayLevel >= UpdateController::ExtendedInfoLevel) {
			DialogMaster::warningT(q->parentWindow(),
								   UpdateController::tr("Check for Updates"),
								   UpdateController::tr("Checking for updates was canceled!"));
		}
		ensureRunning(false);
		return true;
	} else
		return false;
}

#include "moc_updatecontroller.cpp"