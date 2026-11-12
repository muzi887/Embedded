#ifndef AT4GCONFIGURATOR_H
#define AT4GCONFIGURATOR_H

#include <QObject>
#include <QStringList>
#include <QVariantMap>

class DeviceSession;

/*
 * Pushes the MQTT configuration to the WH-LTE-7S1 module by sending the 22 AT
 * commands one by one through the lower board's AT passthrough.
 *
 * Nothing is stored on the MCU any more: the four parameters live only in this
 * application (and, once accepted, inside the module itself).
 */
class At4gConfigurator : public QObject
{
    Q_OBJECT

public:
    explicit At4gConfigurator(DeviceSession *session, QObject *parent = nullptr);

    /* number of commands produced by the last buildCommands()/start() */
    int commandTotal() const;

    /* Build the 22 commands from {productKey, deviceKey, deviceSecret, mqtt_addr}.
     * Returns an empty list and fills `error` when a parameter is unusable. */
    static QStringList buildCommands(const QVariantMap &params, QString *error);

    /* The four "?" queries used by "read all". */
    static QStringList readbackCommands();

    /* Extract the value of a "+TAG:value" reply line, empty when absent. */
    static QString parseReplyValue(const QString &raw, const QString &tag);

    bool isRunning() const;

    void start(const QVariantMap &params);
    void abort();

signals:
    void progress(int done, int total, const QString &command);
    void finished(bool ok, const QString &error);
    void logLine(const QString &text);

private slots:
    void onAtFinished(bool ok, const QString &data, const QString &error);

private:
    void sendCurrent();
    void resendCurrent();
    void stop(bool ok, const QString &error);
    static bool replyIsOk(const QString &data);

    DeviceSession *m_session = nullptr;
    QStringList m_commands;
    int m_index = 0;
    int m_attempt = 0;
    bool m_running = false;
    bool m_abortRequested = false;
};

#endif // AT4GCONFIGURATOR_H
