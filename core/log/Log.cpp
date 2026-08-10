#include "core/log/Log.h"

namespace kh::log {

Q_LOGGING_CATEGORY(core, "bumpcap.core")
Q_LOGGING_CATEGORY(sources, "bumpcap.sources")
Q_LOGGING_CATEGORY(pkg, "bumpcap.pkg")
Q_LOGGING_CATEGORY(boot, "bumpcap.boot")
Q_LOGGING_CATEGORY(repo, "bumpcap.repo")
Q_LOGGING_CATEGORY(config, "bumpcap.config")
Q_LOGGING_CATEGORY(state, "bumpcap.state")
Q_LOGGING_CATEGORY(net, "bumpcap.net")
Q_LOGGING_CATEGORY(notify, "bumpcap.notify")

void InitializeLogging(QtMsgType minimum_type) {
    QString threshold =
        QStringLiteral("*.debug=true\n*.info=true\n*.warning=true\n*.critical=true");
    if (minimum_type == QtInfoMsg) {
        threshold = QStringLiteral("*.debug=false\n*.info=true");
    } else if (minimum_type == QtWarningMsg) {
        threshold = QStringLiteral("*.debug=false\n*.info=false\n*.warning=true");
    } else if (minimum_type == QtCriticalMsg) {
        threshold = QStringLiteral("*.debug=false\n*.info=false\n*.warning=false\n*.critical=true");
    } else if (minimum_type == QtFatalMsg) {
        threshold = QStringLiteral("*.debug=false\n*.info=false\n*.warning=false\n*.critical=false");
    }
    QLoggingCategory::setFilterRules(threshold);
}

}  // namespace kh::log
