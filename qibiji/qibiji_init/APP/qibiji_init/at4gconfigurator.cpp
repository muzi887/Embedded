#include "at4gconfigurator.h"

#include "devicesession.h"

#include <QTimer>

namespace {

constexpr int MaxParamLen = 32;
constexpr int MaxCommandLen = 128;

/*
 * Reconnect policy (AT+SOCKRSTIM / AT+SOCKRSNUM) is optional: it is only sent
 * when the operator fills the fields in, otherwise the module keeps its
 * current settings. Verified on the real module: both accept 1..65535; 0 and 100000
 * are answered with "+CME ERROR:53".
 */

/*
 * The module needs ~10s after the MCU resets it before it answers AT commands,
 * so a command may be lost while it is still booting: retry twice.
 */
constexpr int MaxRetriesPerCommand = 2;
constexpr int RetryDelayMs = 800;

QString formatServer(const QString &addr, bool *ok)
{
    QString out = addr;

    if (out.contains(QLatin1Char(',')))
    {
        *ok = true;
        return out;
    }

    const int pos = out.lastIndexOf(QLatin1Char(':'));
    if (pos <= 0 || pos == out.size() - 1)
    {
        *ok = false;
        return QString();
    }

    out[pos] = QLatin1Char(',');
    *ok = true;
    return out;
}

} // namespace

At4gConfigurator::At4gConfigurator(DeviceSession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    connect(m_session, &DeviceSession::atFinished, this, &At4gConfigurator::onAtFinished);
}

int At4gConfigurator::commandTotal() const
{
    return m_commands.size();
}

QStringList At4gConfigurator::buildCommands(const QVariantMap &params, QString *error)
{
    const QString pk = params.value(QStringLiteral("productKey")).toString().trimmed();
    const QString dk = params.value(QStringLiteral("deviceKey")).toString().trimmed();
    const QString secret = params.value(QStringLiteral("deviceSecret")).toString().trimmed();
    const QString addr = params.value(QStringLiteral("mqtt_addr")).toString().trimmed();

    const QStringList names = { QStringLiteral("productKey"), QStringLiteral("deviceKey"),
                                QStringLiteral("deviceSecret"), QStringLiteral("mqtt_addr") };
    const QStringList values = { pk, dk, secret, addr };

    for (int i = 0; i < names.size(); ++i)
    {
        if (values.at(i).isEmpty())
        {
            if (error != nullptr)
                *error = tr("%1 不能为空").arg(names.at(i));
            return QStringList();
        }
        if (values.at(i).size() > MaxParamLen)
        {
            if (error != nullptr)
                *error = tr("%1 超过 %2 个字符").arg(names.at(i)).arg(MaxParamLen);
            return QStringList();
        }
    }

    bool ok = false;
    const QString server = formatServer(addr, &ok);
    if (!ok)
    {
        if (error != nullptr)
            *error = tr("mqtt_addr 需要“地址:端口”或“地址,端口”");
        return QStringList();
    }

    /* reconnect policy: optional, only sent when the operator filled it in */
    bool sendRstim = false;
    bool sendRsnum = false;
    int rstim = 0;
    int rsnum = 0;
    const QString rstimText = params.value(QStringLiteral("sockrstim")).toString().trimmed();
    const QString rsnumText = params.value(QStringLiteral("sockrsnum")).toString().trimmed();

    if (!rstimText.isEmpty())
    {
        bool parsed = false;
        const int value = rstimText.toInt(&parsed);
        if (!parsed || value < 1 || value > 65535)
        {
            if (error != nullptr)
                *error = tr("重连间隔需为 1–65535 的整数");
            return QStringList();
        }
        rstim = value;
        sendRstim = true;
    }

    if (!rsnumText.isEmpty())
    {
        bool parsed = false;
        const int value = rsnumText.toInt(&parsed);
        if (!parsed || value < 1 || value > 65535)
        {
            if (error != nullptr)
                *error = tr("重连次数需为 1–65535 的整数（0 会被模块拒绝）");
            return QStringList();
        }
        rsnum = value;
        sendRsnum = true;
    }

    QStringList commands;
    commands << QStringLiteral("usr.cn#AT+CSQ?")
             << QStringLiteral("usr.cn#AT+WKMOD=MQTT,NOR")
             << QStringLiteral("usr.cn#AT+MQTTSVR=%1").arg(server)
             << QStringLiteral("usr.cn#AT+MQTTUSER=%1").arg(dk)
             << QStringLiteral("usr.cn#AT+MQTTPSW=%1").arg(secret)
             << QStringLiteral("usr.cn#AT+MQTTCID=%1").arg(dk)
             << QStringLiteral("usr.cn#AT+MQTTMOD=1")
             << QStringLiteral("usr.cn#AT+HEARTEN=OFF");

    if (sendRstim)
        commands << QStringLiteral("usr.cn#AT+SOCKRSTIM=%1").arg(rstim);
    if (sendRsnum)
        commands << QStringLiteral("usr.cn#AT+SOCKRSNUM=%1").arg(rsnum);

    static const char *const pubTopics[] = {
        "up/sys/online",   "up/sys/heartbeat", "up/property/post", "up/event/post",
        "up/alarm/post",   "up/service/reply", "up/ota/request",   "up/request/invoke"
    };
    for (int i = 0; i < 8; ++i)
    {
        commands << QStringLiteral("usr.cn#AT+MQTTPUBTP=%1,1,iot/v1/%2/%3/%4,0,0,0")
                        .arg(i + 1)
                        .arg(pk, dk, QString::fromLatin1(pubTopics[i]));
    }

    static const char *const subTopics[] = {
        "down/service/invoke", "down/config/push", "down/property/get",
        "down/ota/reply",      "down/request/reply"
    };
    for (int i = 0; i < 5; ++i)
    {
        commands << QStringLiteral("usr.cn#AT+MQTTSUBTP=%1,1,iot/v1/%2/%3/%4,0")
                        .arg(i + 1)
                        .arg(pk, dk, QString::fromLatin1(subTopics[i]));
    }

    commands << QStringLiteral("usr.cn#AT+S");

    for (const QString &command : commands)
    {
        if (command.size() > MaxCommandLen)
        {
            if (error != nullptr)
                *error = tr("生成的指令超过 %1 个字符").arg(MaxCommandLen);
            return QStringList();
        }
    }

    return commands;
}

QStringList At4gConfigurator::readbackCommands()
{
    return { QStringLiteral("usr.cn#AT+MQTTSVR?"), QStringLiteral("usr.cn#AT+MQTTUSER?"),
             QStringLiteral("usr.cn#AT+MQTTPSW?"), QStringLiteral("usr.cn#AT+MQTTCID?"),
             QStringLiteral("usr.cn#AT+SOCKRSTIM?"), QStringLiteral("usr.cn#AT+SOCKRSNUM?") };
}

QString At4gConfigurator::parseReplyValue(const QString &raw, const QString &tag)
{
    const QString needle = QLatin1Char('+') + tag + QLatin1Char(':');
    const int pos = raw.indexOf(needle);
    if (pos < 0)
        return QString();

    const int start = pos + needle.size();
    int end = raw.indexOf(QLatin1Char('\r'), start);
    if (end < 0)
        end = raw.indexOf(QLatin1Char('\n'), start);
    if (end < 0)
        end = raw.size();

    return raw.mid(start, end - start).trimmed();
}

bool At4gConfigurator::isRunning() const
{
    return m_running;
}

bool At4gConfigurator::replyIsOk(const QString &data)
{
    if (data.contains(QStringLiteral("ERROR")))
        return false;
    if (data.contains(QStringLiteral("\nOK")))
        return true;
    return data.trimmed().endsWith(QStringLiteral("OK"));
}

void At4gConfigurator::start(const QVariantMap &params)
{
    if (m_running)
        return;

    QString error;
    m_commands = buildCommands(params, &error);
    if (m_commands.isEmpty())
    {
        emit finished(false, error.isEmpty() ? tr("参数不合法") : error);
        return;
    }

    m_index = 0;
    m_abortRequested = false;
    m_running = true;
    sendCurrent();
}

void At4gConfigurator::abort()
{
    if (!m_running)
        return;
    m_abortRequested = true;
    emit logLine(tr("已请求停止，当前指令结束后退出"));
}

void At4gConfigurator::sendCurrent()
{
    if (m_abortRequested)
    {
        stop(false, tr("已手动停止"));
        return;
    }

    if (m_index >= m_commands.size())
    {
        stop(true, QString());
        return;
    }

    m_attempt = 0;
    resendCurrent();
}

void At4gConfigurator::resendCurrent()
{
    if (!m_running)
        return;

    emit progress(m_index, m_commands.size(), m_commands.at(m_index));
    m_session->sendAt(m_commands.at(m_index));
}

void At4gConfigurator::stop(bool ok, const QString &error)
{
    m_running = false;
    m_abortRequested = false;
    emit finished(ok, error);
}

void At4gConfigurator::onAtFinished(bool ok, const QString &data, const QString &error)
{
    if (!m_running)
        return;

    const QString command = m_commands.at(m_index);
    const bool isSave = command.endsWith(QStringLiteral("AT+S"));

    if (ok && replyIsOk(data))
    {
        emit logLine(QStringLiteral("%1/%2 OK  %3")
                         .arg(m_index + 1)
                         .arg(m_commands.size())
                         .arg(command));
    }
    else if (isSave && !data.contains(QStringLiteral("ERROR")))
    {
        /* AT+S stores the configuration and restarts the module, so a timeout
         * or a short reply is expected -- accept it as long as no error came. */
        emit logLine(QStringLiteral("%1/%2 OK (AT+S, no ERROR)  %3")
                         .arg(m_index + 1)
                         .arg(m_commands.size())
                         .arg(command));
    }
    else
    {
        if (m_attempt < MaxRetriesPerCommand)
        {
            ++m_attempt;
            emit logLine(QStringLiteral("%1/%2 无有效应答，重试 %3/%4  %5")
                             .arg(m_index + 1)
                             .arg(m_commands.size())
                             .arg(m_attempt)
                             .arg(MaxRetriesPerCommand)
                             .arg(command));
            QTimer::singleShot(RetryDelayMs, this, &At4gConfigurator::resendCurrent);
            return;
        }

        const QString detail = ok ? data.trimmed() : error;
        emit logLine(QStringLiteral("%1/%2 FAILED  %3  -> %4")
                         .arg(m_index + 1)
                         .arg(m_commands.size())
                         .arg(command, detail));
        stop(false, tr("第 %1/%2 条指令失败：%3")
                        .arg(m_index + 1)
                        .arg(m_commands.size())
                        .arg(detail.isEmpty() ? tr("(无回显)") : detail));
        return;
    }

    ++m_index;
    sendCurrent();
}
