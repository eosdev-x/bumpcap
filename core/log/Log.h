#pragma once

#include <QLoggingCategory>
#include <QtGlobal>

namespace kh::log {

Q_DECLARE_LOGGING_CATEGORY(core)
Q_DECLARE_LOGGING_CATEGORY(sources)
Q_DECLARE_LOGGING_CATEGORY(pkg)
Q_DECLARE_LOGGING_CATEGORY(boot)
Q_DECLARE_LOGGING_CATEGORY(repo)
Q_DECLARE_LOGGING_CATEGORY(config)
Q_DECLARE_LOGGING_CATEGORY(state)
Q_DECLARE_LOGGING_CATEGORY(net)
Q_DECLARE_LOGGING_CATEGORY(notify)

void InitializeLogging(QtMsgType minimum_type = QtInfoMsg);

}  // namespace kh::log

