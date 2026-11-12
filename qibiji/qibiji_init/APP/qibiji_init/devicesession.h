#ifndef DEVICESESSION_H
#define DEVICESESSION_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "sbf20codec.h"

class QSerialPort;
class QTimer;

/*
 * SBF-20 initiator for the Qt upper computer.
 *
 * The application is always the session initiator: it splits the request JSON
 * into payloads of at most 15 bytes, sends one frame per pair and waits for the
 * matching response frame before sending the next one.
 */
class DeviceSession : public QObject
{
    Q_OBJECT

public:
    explicit DeviceSession(QObject *parent = nullptr);
    ~DeviceSession() override;

    static QStringList availablePorts();

    bool open(const QString &portName, qint32 baudRate, QString *errorMessage = nullptr);
    void close();
    bool isOpen() const;
    QString portName() const;
    qint32 baudRate() const;

    bool isBusy() const;

    void probe();
    void readConfig();
    void writeConfig(const QVariantMap &params);
    void sendAt(const QString &command);
    void sendAt3(const QString &command);

signals:
    void openChanged(bool isOpen);
    void probeFinished(bool ok);
    void readFinished(bool ok, const QVariantMap &params, const QString &error);
    void writeFinished(bool ok, const QString &error);
    void atFinished(bool ok, const QString &data, const QString &error);
    void at3Finished(bool ok, const QString &data, const QString &error);
    void frameTraced(const QString &direction, const QByteArray &frame);
    void requestTraced(const QString &json);

private slots:
    void onReadyRead();
    void onPairTimeout();

private:
    enum class Op { None, Probe, Read, Write, At, At3 };

    void startSession(Op op, const QByteArray &json);
    void sendNextFrame();
    void handleFrame(const Sbf20Frame &frame);
    void dispatchResponse();
    void failSession(const QString &error);
    void emitRequestFailure(Op op, const QString &error);
    void abortSession();
    int pairTimeoutMs() const;

    QSerialPort *m_port = nullptr;
    QTimer *m_pairTimer = nullptr;
    Sbf20Parser m_parser;

    Op m_op = Op::None;
    QByteArray m_request;
    int m_reqOffset = 0;
    int m_index = 0;
    bool m_lastRspMore = false;
    QByteArray m_response;
};

#endif // DEVICESESSION_H
