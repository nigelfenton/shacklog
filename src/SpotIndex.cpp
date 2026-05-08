#include "SpotIndex.h"

#include <QDateTime>
#include <cmath>

namespace ShackLog {

SpotIndex::SpotIndex(QObject* parent) : QObject(parent) {}

int SpotIndex::stepHzForMode(const QString& adifMode)
{
    const QString m = adifMode.toUpper();
    if (m == "CW" || m == "RTTY")            return 100;
    if (m == "FT8" || m == "FT4" || m == "PSK" ||
        m == "JT65" || m == "JT9" || m == "JS8" ||
        m == "MFSK")                          return 100;
    // SSB / AM / FM and any unknown mode → 1 kHz (channelised step,
    // covers the typical phone wiggle of a few hundred Hz)
    return 1000;
}

qint64 SpotIndex::roundedHz(double freqMhz, int stepHz)
{
    if (stepHz <= 0) stepHz = 1;
    const qint64 hz = static_cast<qint64>(std::llround(freqMhz * 1.0e6));
    // Round to nearest step (not floor), so 14250500 with 1 kHz step
    // lands on 14251000, not 14250000.
    return ((hz + stepHz / 2) / stepHz) * stepHz;
}

qint64 SpotIndex::bucketKey(double freqMhz, const QString& adifMode)
{
    const int step = stepHzForMode(adifMode);
    const qint64 hz = roundedHz(freqMhz, step);
    // Mix a small mode-tag into the upper bits so different modes at the
    // same step never collide.  qHash on a QString would also work but
    // overkill — base-mode ADIF strings are short.
    qint64 modeTag = 0;
    const QString m = adifMode.toUpper();
    if      (m == "SSB")  modeTag = 1;
    else if (m == "CW")   modeTag = 2;
    else if (m == "AM")   modeTag = 3;
    else if (m == "FM")   modeTag = 4;
    else if (m == "RTTY") modeTag = 5;
    else if (m == "FT8" || m == "FT4" || m == "PSK" ||
             m == "JS8" || m == "MFSK")
                          modeTag = 6;          // generic digital bucket
    return hz | (modeTag << 40);
}

void SpotIndex::addOrUpdate(const SpotData& spot)
{
    if (spot.call.isEmpty()) return;
    SpotData s = spot;
    if (!s.receivedAt.isValid()) s.receivedAt = QDateTime::currentDateTimeUtc();

    // If an older entry for this call exists, drop its bucket pointer
    // (its freq may differ from the new one).
    if (auto old = m_byCall.constFind(s.call); old != m_byCall.constEnd()) {
        const qint64 oldKey = bucketKey(old->freqMhz, old->mode);
        if (m_byBucket.value(oldKey) == s.call) m_byBucket.remove(oldKey);
    }

    m_byCall.insert(s.call, s);
    m_byBucket.insert(bucketKey(s.freqMhz, s.mode), s.call);
    emit spotAdded(s);
}

int SpotIndex::purgeExpired()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    int removed = 0;
    for (auto it = m_byCall.begin(); it != m_byCall.end(); ) {
        const qint64 ageSec = it->receivedAt.secsTo(now);
        if (ageSec > it->lifetimeSec) {
            const qint64 key = bucketKey(it->freqMhz, it->mode);
            if (m_byBucket.value(key) == it->call) m_byBucket.remove(key);
            it = m_byCall.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0) emit spotsRemoved(removed);
    return removed;
}

std::optional<SpotData> SpotIndex::findAt(double freqMhz, const QString& adifMode) const
{
    if (freqMhz <= 0.0) return std::nullopt;

    // Direct bucket lookup — exact mode match.
    const qint64 key = bucketKey(freqMhz, adifMode);
    auto it = m_byBucket.constFind(key);
    if (it != m_byBucket.constEnd()) {
        auto cIt = m_byCall.constFind(*it);
        if (cIt != m_byCall.constEnd()) return *cIt;
    }

    // Fallback — same step bucket but mode unknown (some DX cluster
    // spots come without a mode).  Look for any spot in the same Hz
    // bucket regardless of mode tag.
    if (!adifMode.isEmpty()) {
        const int step = stepHzForMode(adifMode);
        const qint64 hz = roundedHz(freqMhz, step);
        for (auto k = m_byBucket.constBegin(); k != m_byBucket.constEnd(); ++k) {
            if ((k.key() & 0xFFFFFFFFFFLL) == hz) {
                auto cIt = m_byCall.constFind(k.value());
                if (cIt != m_byCall.constEnd() && cIt->mode.isEmpty()) {
                    return *cIt;
                }
            }
        }
    }
    return std::nullopt;
}

} // namespace ShackLog
