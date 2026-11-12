#include "devicesession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QVariant>

namespace {

/* Per pair timeouts. Writing the EEPROM takes about 1.3s on the MCU and an AT
 * command may wait up to 1.5s for the modem, so those ops need a longer pair
 * timeout than the 500ms the transport layer normally uses. */
constexpr int ProbeTimeoutMs = 500;
constexpr int ReadTimeoutMs = 1500;
/* A single write may carry MQTT strings + gate words + the 44 byte OTA block;
 * the MCU writes the EEPROM byte by byte with a 10ms delay, ~1.8s worst case. */
constexpr int WriteTimeoutMs = 5000;
constexpr int AtTimeoutMs = 3000;
/* BLE AT commands answer in a few ms; keep this short so a missing module is
 * reported quickly. */
constexpr int At3TimeoutMs = 2000;

} // namespace

DeviceSession::DeviceSession(QObject *parent)
    : QObject(parent)
    , m_port(new QSerialPort(this))
    , m_pairTimer(new QTimer(this))
{
    m_pairTimer->setSingleShot(true);
    connect(m_pairTimer, &QTimer::timeout, this, &DeviceSession::onPairTimeout);
    connect(m_port, &QSerialPort::readyRead, this, &DeviceSession::onReadyRead);
}

DeviceSession::~DeviceSession() = default;

QStringList DeviceSession::availablePorts()
{
    QStringList names;
    const QList<QSerialPortInfo> infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos)
        names.append(info.portName());
    names.sort();
    return names;
}

bool DeviceSession::open(const QString &portName, qint32 baudRate, QString *errorMessage)
{
    close();

    m_port->setPortName(portName);
    m_port->setBaudRate(baudRate);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite))
    {
        if (errorMessage != nullptr)
            *errorMessage = m_port->errorString();
        return false;
    }

    m_parser.reset();
    emit openChanged(true);
    return true;
}

void DeviceSession::close()
{
    abortSession();
    if (m_port->isOpen())
    {
        m_port->close();
        emit openChanged(false);
    }
}

bool DeviceSession::isOpen() const
{
    return m_port->isOpen();
}

QString DeviceSession::portName() const
{
    return m_port->portName();
}

qint32 DeviceSession::baudRate() const
{
    return m_port->baudRate();
}

bool DeviceSession::isBusy() const
{
    return m_op != Op::None;
}

void DeviceSession::emitRequestFailure(Op op, const QString &error)
{
    switch (op)
    {
    case Op::Probe:
        emit probeFinished(false);
        break;
    case Op::Read:
        emit readFinished(false, QVariantMap(), error);
        break;
    case Op::Write:
        emit writeFinished(false, error);
        break;
    case Op::At:
        emit atFinished(false, QString(), error);
        break;
    case Op::At3:
        emit at3Finished(false, QString(), error);
        break;
    case Op::None:
        break;
    }
}

int DeviceSession::pairTimeoutMs() const
{
    switch (m_op)
    {
    case Op::Read:
        return ReadTimeoutMs;
    case Op::Write:
        return WriteTimeoutMs;
    case Op::At:
        return AtTimeoutMs;
    case Op::At3:
        return At3TimeoutMs;
    case Op::Probe:
    case Op::None:
    default:
        return ProbeTimeoutMs;
    }
}

void DeviceSession::startSession(Op op, const QByteArray &json)
{
    if (m_op != Op::None)
    {
        emitRequestFailure(op, tr("上一次操作尚未结束"));
        return;
    }

    if (!m_port->isOpen())
    {
        emitRequestFailure(op, tr("串口未打开"));
        return;
    }

    m_op = op;
    m_request = json;
    m_reqOffset = 0;
    m_index = 0;
    m_lastRspMore = false;
    m_response.clear();

    if (!json.isEmpty())
        emit requestTraced(QString::fromUtf8(json));

    sendNextFrame();
}

void DeviceSession::sendNextFrame()
{
    QByteArray payload;
    bool more = false;

    if (m_reqOffset < m_request.size())
    {
        const int chunk = qMin(Sbf20::PayloadMax, m_request.size() - m_reqOffset);
        payload = m_request.mid(m_reqOffset, chunk);
        m_reqOffset += chunk;
        more = (m_reqOffset < m_request.size());
    }
    else
    {
        /* Request fully sent: keep pulling the response with an empty payload.
         * MORE stays 0 on this side; the responder signals "response finished"
         * with its own MORE=0. */
        more = false;
    }

    const QByteArray frame = Sbf20::encodeFrame(m_index, more, payload);
    if (frame.isEmpty())
    {
        failSession(tr("请求报文过长"));
        return;
    }

    m_port->write(frame);
    m_port->flush();
    emit frameTraced(QStringLiteral("TX"), frame);
    m_pairTimer->start(pairTimeoutMs());
}

void DeviceSession::onReadyRead()
{
    const QByteArray bytes = m_port->readAll();
    if (bytes.isEmpty())
        return;

    const QList<Sbf20Frame> frames = m_parser.feed(bytes);
    for (const Sbf20Frame &frame : frames)
    {
        emit frameTraced(QStringLiteral("RX"),
                         Sbf20::encodeFrame(frame.index, frame.more, frame.payload));
        handleFrame(frame);
    }
}

void DeviceSession::handleFrame(const Sbf20Frame &frame)
{
    if (m_op == Op::None)
        return;

    if (m_op == Op::Probe)
    {
        m_pairTimer->stop();
        m_op = Op::None;
        emit probeFinished(frame.payload.isEmpty() && !frame.more);
        return;
    }

    if (frame.index != m_index)
        return; /* stale or unexpected pair */

    m_pairTimer->stop();
    m_response.append(frame.payload);
    m_lastRspMore = frame.more;

    if (!frame.more)
    {
        dispatchResponse();
        return;
    }

    ++m_index;
    sendNextFrame();
}

void DeviceSession::dispatchResponse()
{
    const Op op = m_op;
    m_op = Op::None;
    m_pairTimer->stop();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(m_response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        emitRequestFailure(op, tr("应答 JSON 解析失败"));
        return;
    }

    const QJsonObject object = document.object();
    const int code = object.value(QStringLiteral("code")).toInt(-1);
    const QString message = object.value(QStringLiteral("message")).toString();
    const QString error = (code == 0)
                              ? QString()
                              : (message.isEmpty() ? tr("错误码 %1").arg(code) : message);

    switch (op)
    {
    case Op::Read:
        if (code == 0 && object.value(QStringLiteral("params")).isObject())
            emit readFinished(true, object.value(QStringLiteral("params")).toObject().toVariantMap(),
                              QString());
        else
            emit readFinished(false, QVariantMap(), error);
        break;

    case Op::Write:
        emit writeFinished(code == 0, error);
        break;

    case Op::At:
        emit atFinished(code == 0, object.value(QStringLiteral("data")).toString(), error);
        break;

    case Op::At3:
        emit at3Finished(code == 0, object.value(QStringLiteral("data")).toString(), error);
        break;

    case Op::Probe:
    case Op::None:
    default:
        break;
    }
}

void DeviceSession::failSession(const QString &error)
{
    const Op op = m_op;
    m_op = Op::None;
    m_pairTimer->stop();
    emitRequestFailure(op, error);
}

void DeviceSession::onPairTimeout()
{
    failSession(tr("等待下位机应答超时"));
}

void DeviceSession::abortSession()
{
    m_op = Op::None;
    m_pairTimer->stop();
    m_response.clear();
    m_parser.reset();
}

void DeviceSession::probe()
{
    startSession(Op::Probe, QByteArray());
}

void DeviceSession::readConfig()
{
    QJsonObject root;
    root.insert(QStringLiteral("op"), QStringLiteral("read"));
    /* no "configs" -> the lower board returns every group (mqtt + gate + ota) */
    startSession(Op::Read, QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void DeviceSession::writeConfig(const QVariantMap &params)
{
    QJsonObject paramsObject;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
    {
        const QVariant &value = it.value();
        if (!value.isValid())
            continue; /* never send a placeholder as an empty string */
        if (value.typeId() == QMetaType::QString)
            paramsObject.insert(it.key(), value.toString());
        else if (value.canConvert<qlonglong>())
            paramsObject.insert(it.key(), QJsonValue(value.toLongLong()));
        else
            paramsObject.insert(it.key(), value.toString());
    }

    QJsonObject root;
    root.insert(QStringLiteral("op"), QStringLiteral("write"));
    root.insert(QStringLiteral("params"), paramsObject);
    startSession(Op::Write, QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void DeviceSession::sendAt(const QString &command)
{
    QJsonObject root;
    root.insert(QStringLiteral("op"), QStringLiteral("at"));
    root.insert(QStringLiteral("cmd"), command);
    startSession(Op::At, QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void DeviceSession::sendAt3(const QString &command)
{
    QJsonObject root;
    root.insert(QStringLiteral("op"), QStringLiteral("at3"));
    root.insert(QStringLiteral("cmd"), command);
    startSession(Op::At3, QJsonDocument(root).toJson(QJsonDocument::Compact));
}
