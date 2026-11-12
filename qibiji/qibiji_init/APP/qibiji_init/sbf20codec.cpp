#include "sbf20codec.h"

namespace Sbf20 {

quint8 crc8Maxim(const QByteArray &data)
{
    quint8 crc = 0x00;

    for (int i = 0; i < data.size(); ++i)
    {
        crc ^= static_cast<quint8>(data.at(i));
        for (int b = 0; b < 8; ++b)
        {
            if (crc & 0x01u)
                crc = static_cast<quint8>((crc >> 1) ^ 0x8Cu);
            else
                crc = static_cast<quint8>(crc >> 1);
        }
    }
    return crc;
}

QByteArray encodeFrame(int index, bool more, const QByteArray &payload)
{
    if (payload.size() > PayloadMax)
        return QByteArray();

    const quint8 flags = static_cast<quint8>((more ? 0x80u : 0x00u) |
                                             (static_cast<unsigned>(index) & 0x7Fu));

    QByteArray data;
    data.append(static_cast<char>(flags));
    data.append(payload);

    QByteArray frame;
    frame.append(static_cast<char>(Sof));
    frame.append(static_cast<char>(data.size() + 1));
    frame.append(data);
    frame.append(static_cast<char>(crc8Maxim(frame)));
    return frame;
}

} // namespace Sbf20

void Sbf20Parser::reset()
{
    m_state = WaitSof;
    m_frame.clear();
    m_need = 0;
}

QList<Sbf20Frame> Sbf20Parser::feed(const QByteArray &bytes)
{
    QList<Sbf20Frame> frames;

    for (int i = 0; i < bytes.size(); ++i)
    {
        const quint8 b = static_cast<quint8>(bytes.at(i));

        switch (m_state)
        {
        case WaitSof:
            if (b == Sbf20::Sof)
            {
                m_frame.clear();
                m_frame.append(static_cast<char>(b));
                m_state = WaitLen;
            }
            break;

        case WaitLen:
            if (b < Sbf20::LenMin || b > Sbf20::LenMax)
            {
                if (b == Sbf20::Sof)
                {
                    m_frame.clear();
                    m_frame.append(static_cast<char>(b));
                    m_state = WaitLen;
                }
                else
                {
                    m_frame.clear();
                    m_state = WaitSof;
                }
                break;
            }
            m_frame.append(static_cast<char>(b));
            m_need = b;
            m_state = WaitBody;
            break;

        case WaitBody:
            m_frame.append(static_cast<char>(b));
            if (m_frame.size() >= 2 + m_need)
            {
                if (Sbf20::crc8Maxim(m_frame) == 0)
                {
                    const char flags = m_frame.at(2);
                    Sbf20Frame frame;
                    frame.index = static_cast<quint8>(flags) & 0x7F;
                    frame.more = (static_cast<quint8>(flags) & 0x80u) != 0;
                    frame.payload = m_frame.mid(3, m_need - 2);
                    frames.append(frame);
                }
                m_frame.clear();
                m_state = WaitSof;
            }
            break;

        default:
            m_frame.clear();
            m_state = WaitSof;
            break;
        }
    }

    return frames;
}
