#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

#include "core/config/ConfigManager.h"
#include "core/notify/Notifier.h"
#include "core/pkg/DnfCliBackend.h"
#include "core/repo/KernelRepository.h"
#include "core/sources/CachyOsCoprSource.h"
#include "core/sources/FedoraRawhideSource.h"
#include "core/sources/FedoraStableSource.h"
#include "core/state/StateStore.h"
#include "gui/MainWindow.h"

#include <memory>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Bumpcap"));
    QApplication::setApplicationDisplayName(QStringLiteral("Bumpcap"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("Bumpcap"));
    QApplication::setDesktopFileName(QStringLiteral("org.bumpcap.Bumpcap"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/kh/gui/icons/bumpcap.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Browse, install, and manage Linux kernels on Fedora."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    kh::config::ConfigManager config_manager;
    config_manager.load();

    kh::state::StateStore state_store;
    state_store.open();

    kh::repo::KernelRepository repository;
    repository.setStateStore(&state_store);

    // Wire up package backend
    auto package_backend = std::make_unique<kh::pkg::DnfCliBackend>();
    repository.setPackageBackend(package_backend.get());
    package_backend.release();

    // Wire up kernel sources
    auto fedora_stable = std::make_shared<kh::sources::FedoraStableSource>();
    repository.addSource(fedora_stable);

    auto fedora_rawhide = std::make_shared<kh::sources::FedoraRawhideSource>();
    repository.addSource(fedora_rawhide);

    auto cachyos_stable = std::make_shared<kh::sources::CachyOsCoprSource>(
        kh::sources::CachyOsCoprSource::Variant::Stable);
    repository.addSource(cachyos_stable);

    auto cachyos_lts = std::make_shared<kh::sources::CachyOsCoprSource>(
        kh::sources::CachyOsCoprSource::Variant::Lts);
    repository.addSource(cachyos_lts);

    kh::notify::Notifier notifier;
    notifier.setApplicationName(QStringLiteral("Bumpcap"));

    kh::gui::MainWindow window(&repository,
                               nullptr,
                               nullptr,
                               &config_manager,
                               &state_store,
                               &notifier);
    window.resize(1120, 720);
    window.show();

    repository.refresh();
    return QApplication::exec();
}
