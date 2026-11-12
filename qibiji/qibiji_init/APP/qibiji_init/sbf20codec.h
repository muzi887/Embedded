#ifndef SBF20CODEC_H
#define SBF20CODEC_H

#include <QByteArray>
#include <QList>
#include <QtGlobal>

/*
 * SBF-20 wire format:
 *
 *   [0]     SOF   0x55
 *   [1]     LEN   DATA size + 1 (CRC8 included), 2..17
 *   [2..]   DATA  FLAGS(1B) + PAYLOAD(0..15B)
 *   [2+n]   CRC8  CRC8/Maxim over SOF+LEN+DATA -> 0 for a complete good frame
 *
 * FLAGS = (MORE << 7) | (INDEX & 0x7F)
 */
namespace Sbf20 {

constexpr quint8 Sof = 0x55;
constexpr int LenMin = 2;
constexpr int LenMax = 17;
constexpr int DataMax = 16;
constexpr int PayloadMax = 15;
constexpr int FrameMax = 19;

quint8 crc8Maxim(const QByteArray &data);

/* Build a complete line frame; returns an empty array when the payload is too
 * long. */
QByteArray encodeFrame(int index, bool more, const QByteArray &payload);

} // namespace Sbf20

struct Sbf20Frame
{
    int index = 0;
    bool more = false;
    QByteArray payload;
};

/*
 * Byte stream -> frames. Garbage such as the firmware debug printf output is
 * skipped by resynchronising on SOF + LEN + CRC8.
 */
class Sbf20Parser
{
public:
    void reset();
    QList<Sbf20Frame> feed(const QByteArray &bytes);

private:
    enum State { WaitSof, WaitLen, WaitBody };

    State m_state = WaitSof;
    QByteArray m_frame;
    int m_need = 0;
};

#endif // SBF20CODEC_H
