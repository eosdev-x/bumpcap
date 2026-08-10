#include "core/net/NetworkClient.h"

#include <QNetworkRequest>
#include <QTimer>

#include "core/log/Log.h"

namespace kh::net {

NetworkClient::NetworkClient(QObject *parent) : QObject(parent), manager_(this) {}

int NetworkClient::get(const QUrl &url, int max_retries) {
    const int request_id = next_request_id_++;
    requests_.insert(request_id, RequestState{url, qMax(0, max_retries), 0});
    startRequest(request_id);
    return request_id;
}

void NetworkClient::setBaseBackoffMilliseconds(int milliseconds) {
    base_backoff_milliseconds_ = qMax(1, milliseconds);
}

void NetworkClient::startRequest(int request_id) {
    auto it = requests_.find(request_id);
    if (it == requests_.end()) {
        return;
    }
    it->attempt += 1;

    QNetworkRequest request(it->url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager_.get(request);
    it->reply = reply;
    QObject::connect(reply, &QNetworkReply::finished, this, [this, request_id, reply]() {
        finishReply(request_id, reply);
    });
    QTimer::singleShot(timeout_milliseconds_, this, [this, request_id, reply]() {
        timeoutRequest(request_id, reply);
    });
}

void NetworkClient::finishReply(int request_id, QNetworkReply *reply) {
    auto it = requests_.find(request_id);
    if (it == requests_.end() || it->reply != reply) {
        reply->deleteLater();
        return;
    }

    const int http_status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = reply->error();
    const QByteArray body = reply->readAll();
    const QString error_string = reply->errorString();
    reply->deleteLater();

    if (error == QNetworkReply::NoError && http_status < 400) {
        requests_.erase(it);
        emit requestFinished(request_id, body, http_status);
        return;
    }

    if ((http_status >= 500 ||
         (http_status == 0 && error != QNetworkReply::NoError)) &&
        it->attempt <= it->maxRetries) {
        qCInfo(kh::log::net) << "Retrying request" << request_id << it->url
                             << "after network error" << error;
        retryLater(request_id);
        return;
    }

    requests_.erase(it);
    const QString message =
        http_status >= 400
            ? QStringLiteral("HTTP request failed with status %1").arg(http_status)
            : error_string;
    emit requestFailed(request_id, message, http_status);
}

void NetworkClient::timeoutRequest(int request_id, QNetworkReply *reply) {
    auto it = requests_.find(request_id);
    if (it == requests_.end() || it->reply != reply || reply->isFinished()) {
        return;
    }
    reply->abort();
}

void NetworkClient::retryLater(int request_id) {
    const RequestState state = requests_.value(request_id);
    const int delay = base_backoff_milliseconds_ * (1 << qMax(0, state.attempt - 1));
    QTimer::singleShot(delay, this, [this, request_id]() {
        startRequest(request_id);
    });
}

}  // namespace kh::net
