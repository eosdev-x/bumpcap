#pragma once

#include <QByteArray>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QUrl>

namespace kh::net {

class NetworkClient : public QObject {
    Q_OBJECT

public:
    explicit NetworkClient(QObject *parent = nullptr);

    int get(const QUrl &url, int max_retries = 3);
    void setBaseBackoffMilliseconds(int milliseconds);

signals:
    void requestFinished(int requestId, QByteArray body, int httpStatusCode);
    void requestFailed(int requestId, QString errorMessage, int httpStatusCode);

private:
    struct RequestState {
        QUrl url;
        int maxRetries = 3;
        int attempt = 0;
        QNetworkReply *reply = nullptr;
    };

    void startRequest(int request_id);
    void finishReply(int request_id, QNetworkReply *reply);
    void timeoutRequest(int request_id, QNetworkReply *reply);
    void retryLater(int request_id);

    QNetworkAccessManager manager_;
    QHash<int, RequestState> requests_;
    int next_request_id_ = 1;
    int base_backoff_milliseconds_ = 1000;
    int timeout_milliseconds_ = 30000;
};

}  // namespace kh::net
