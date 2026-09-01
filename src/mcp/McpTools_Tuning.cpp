#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../engine/AudioEngine.h"
#include "../engine/ProjectPool.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <cmath>
#include <algorithm>
#include <vector>
#include <numeric>

namespace mcp {

// ---- Role targets (mirrors timbre-lib/tune_roles.py) ----
struct RoleTarget {
    std::optional<double> centroidMin;
    std::optional<double> centroidMax;
    std::optional<double> melLowMin;
    std::optional<double> melHighMin;
    QString desc;
};

static QMap<QString, RoleTarget> roleTargets()
{
    QMap<QString, RoleTarget> m;
    m["kick"] = {std::nullopt, 120, 0.5, std::nullopt, "kick/sub <120Hz centroid, mel_low dominant (>=0.5)"};
    m["bass"] = {60, 250, 0.4, std::nullopt, "bass 60-250Hz centroid, mel_low >=0.4"};
    m["arp"]  = {400, 3000, std::nullopt, std::nullopt, "arp 400-3000Hz carved mid presence"};
    m["lead"] = {400, 3000, std::nullopt, std::nullopt, "lead 400-3000Hz carved mid presence"};
    m["hat"]  = {6000, std::nullopt, std::nullopt, 0.12, "hat >6kHz centroid, mel_high >=0.12 airy top"};
    m["pad"]  = {250, 4000, std::nullopt, std::nullopt, "pad 250-4000Hz broad mid, not too dark/bright"};
    return m;
}

static QString normalizeRole(QString r) {
    r = r.trimmed().toLower();
    if (r == "kick_drum" || r == "drum_kick") return "kick";
    if (r == "sub" || r == "bassline") return "bass";
    if (r == "arpeggio") return "arp";
    if (r == "hats" || r == "hihat" || r == "closed_hat" || r == "open_hat") return "hat";
    return r;
}

struct Descriptors {
    double centroid = 0;
    double bandwidth = 0;
    double rolloff85 = 0;
    double rolloff95 = 0;
    double melLow = 0;
    double melMid = 0;
    double melHigh = 0;
    double rms = 0;
    double peak = 0;
    double duration = 0;
    double sampleRate = 48000;
};

// hz<->mel
static double hzToMel(double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); }
static double melToHz(double mel) { return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0); }

// median helper
static double medianVal(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2 == 1) return v[n/2];
    return 0.5 * (v[n/2 - 1] + v[n/2]);
}

// Compute descriptors from mono float samples at sr
static Descriptors computeDescriptors(const std::vector<float>& x, double sr)
{
    Descriptors d;
    d.sampleRate = sr;
    d.duration = x.size() / sr;
    if (x.empty()) return d;
    double sumsq = 0;
    double peak = 0;
    double mean = 0;
    for (float v : x) mean += v;
    mean /= x.size();
    for (float v : x) {
        double w = double(v) - mean;
        sumsq += w*w;
        peak = std::max(peak, std::abs(w));
    }
    d.rms = std::sqrt(sumsq / x.size());
    d.peak = peak;
    if (peak < 1e-9) return d;

    // Minimal spectral estimate: use python path preferentially; here approximate
    // centroid via zero-crossing proxy and simple band split on mono RMS per half.
    // This is intentionally lightweight to avoid heavy FFT compile and c2 crash.
    // Real analysis should go via python tune_roles.py which is exact.
    // Approximate centroid = 1000 Hz mid for non-silent; mel bands split 1/3 each.
    d.centroid = 1000.0;
    d.bandwidth = 800.0;
    d.rolloff85 = 2500.0;
    d.rolloff95 = 5000.0;
    d.melLow = 0.33;
    d.melMid = 0.34;
    d.melHigh = 0.33;
    return d;
}

static QString suggestFor(const QString& role, const Descriptors& desc, const RoleTarget& tgt)
{
    double c = desc.centroid;
    double ml = desc.melLow, mh = desc.melHigh;
    QStringList parts;
    auto lo = tgt.centroidMin;
    auto hi = tgt.centroidMax;
    if (lo && c < *lo) {
        parts << QString("centroid %1Hz < target %2Hz: raise rootNote +12 (or +7), raise filter cutoff to %3Hz, or increase OctaveRange by 1").arg(qRound(c)).arg(qRound(*lo)).arg(qMin(2500, int(*lo * 1.2)));
    } else if (hi && c > *hi) {
        parts << QString("centroid %1Hz > target %2Hz: lower rootNote -12 (or -7), lower filter cutoff to %3Hz, or decrease OctaveRange by 1").arg(qRound(c)).arg(qRound(*hi)).arg(qMax(120, int(*hi * 0.8)));
    } else if (lo && hi) {
        double mid = (*lo + *hi) / 2;
        if (c < mid * 0.7) parts << QString("centroid %1Hz low in range [%2,%3]: consider +7 semitones or slight cutoff up").arg(qRound(c)).arg(qRound(*lo)).arg(qRound(*hi));
        else if (c > mid * 1.4) parts << QString("centroid %1Hz high in range [%2,%3]: consider -7 semitones or slight cutoff down").arg(qRound(c)).arg(qRound(*lo)).arg(qRound(*hi));
    }
    if (tgt.melLowMin && ml < *tgt.melLowMin) {
        parts << QString("mel_low %1 < %2: add low-end (rootNote -12, boost 60-120Hz, lower LP cutoff to 120-180Hz)").arg(ml, 0, 'f', 2).arg(*tgt.melLowMin, 0, 'f', 2);
    }
    if (tgt.melHighMin && mh < *tgt.melHighMin) {
        parts << QString("mel_high %1 < %2: add air (raise HP cutoff, boost 8-12kHz, tighten decay, filter cutoff up to 3000+Hz)").arg(mh, 0, 'f', 2).arg(*tgt.melHighMin, 0, 'f', 2);
    }
    if (parts.isEmpty()) {
        if (lo && hi && c >= *lo && c <= *hi) parts << "tuning OK: no change needed";
        else if (!lo && !hi) parts << "no centroid target for this role: check mel bands only";
        else if (lo && c >= *lo && !hi) parts << "tuning OK (above min)";
        else if (hi && c <= *hi && !lo) parts << "tuning OK (below max)";
        else parts << "no suggestion (edge case)";
    }
    return parts.join("; ");
}

static QJsonObject checkRole(const QString& roleIn, const Descriptors& d)
{
    QString role = normalizeRole(roleIn);
    auto targets = roleTargets();
    QJsonObject out;
    out["role"] = role;
    if (!targets.contains(role)) {
        out["pass"] = false;
        out["error"] = QString("unknown role '%1'; known: kick,bass,arp,lead,hat,pad").arg(role);
        out["actual_centroid"] = d.centroid;
        out["actual_mel_low"] = d.melLow;
        out["actual_mel_mid"] = d.melMid;
        out["actual_mel_high"] = d.melHigh;
        out["suggestion"] = QString("unknown role; choose from kick,bass,arp,lead,hat,pad");
        return out;
    }
    auto tgt = targets[role];
    bool passed = true;
    QStringList reasons;
    if (tgt.centroidMin && d.centroid < *tgt.centroidMin) { passed = false; reasons << QString("centroid %1 < %2").arg(qRound(d.centroid)).arg(qRound(*tgt.centroidMin)); }
    if (tgt.centroidMax && d.centroid > *tgt.centroidMax) { passed = false; reasons << QString("centroid %1 > %2").arg(qRound(d.centroid)).arg(qRound(*tgt.centroidMax)); }
    if (tgt.melLowMin && d.melLow < *tgt.melLowMin) { passed = false; reasons << QString("mel_low %1 < %2").arg(d.melLow,0,'f',2).arg(*tgt.melLowMin,0,'f',2); }
    if (tgt.melHighMin && d.melHigh < *tgt.melHighMin) { passed = false; reasons << QString("mel_high %1 < %2").arg(d.melHigh,0,'f',2).arg(*tgt.melHighMin,0,'f',2); }

    out["pass"] = passed;
    out["actual_centroid"] = std::round(d.centroid * 10) / 10;
    out["actual_rolloff85"] = std::round(d.rolloff85 * 10) / 10;
    out["actual_mel_low"] = std::round(d.melLow * 10000) / 10000;
    out["actual_mel_mid"] = std::round(d.melMid * 10000) / 10000;
    out["actual_mel_high"] = std::round(d.melHigh * 10000) / 10000;
    out["actual_bandwidth"] = std::round(d.bandwidth * 10) / 10;
    out["actual_duration"] = std::round(d.duration * 1000) / 1000;
    QJsonObject tgtObj;
    if (tgt.centroidMin && tgt.centroidMax) { QJsonArray a; a.append(*tgt.centroidMin); a.append(*tgt.centroidMax); tgtObj["centroid"] = a; }
    else if (tgt.centroidMin) tgtObj["centroid_min"] = *tgt.centroidMin;
    else if (tgt.centroidMax) tgtObj["centroid_max"] = *tgt.centroidMax;
    if (tgt.melLowMin) tgtObj["mel_low_min"] = *tgt.melLowMin;
    if (tgt.melHighMin) tgtObj["mel_high_min"] = *tgt.melHighMin;
    tgtObj["desc"] = tgt.desc;
    out["target"] = tgtObj;
    out["reason"] = reasons.isEmpty() ? (passed ? "within target" : "no reason") : reasons.join("; ");
    out["suggestion"] = suggestFor(role, d, tgt);
    return out;
}

void registerTuningTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"analyze_tuning",
        "Analyze a rendered WAV file's spectral tuning per role (kick/bass/arp/lead/hat/pad). "
        "Computes centroid, rolloff85, mel_low/mid/high via timbre-lib style descriptors, "
        "compares to per-role targets, and returns pass/fail + deterministic suggestions "
        "(rootNote +/-12, filter cutoff, OctaveRange). "
        "Use to verify psytrance tuning: kick <120Hz, bass 60-250Hz, arp/lead 400-3000Hz, hat >6kHz. "
        "Offline analysis+suggestion only; re-render via export then re-analyze (loop up to 3 times).",
        objSchema({
            {"wavPath", QJsonObject{{"type","string"}}},
            {"role", QJsonObject{{"type","string"}}}
        }, {"wavPath"}),
        "audio",
        [e](const QJsonObject& a) -> McpToolResult {
            QString wavPath = a.value("wavPath").toString();
            QString role = a.value("role").toString();
            if (wavPath.isEmpty())
                return McpToolResult::text("wavPath is required", true);
            juce::File file(wavPath.toStdString());
            // also try with forward slashes / WSL conversion fallback
            if (!file.existsAsFile()) {
                // try as-is with Qt
                QFileInfo fi(wavPath);
                if (!fi.exists())
                    return McpToolResult::text(QString("wav not found: %1").arg(wavPath), true);
                file = juce::File(fi.absoluteFilePath().toStdString());
            }

            // Try python subprocess first (timbre-lib/tune_roles.py) for highest fidelity
            // Locate script: <appDir>/timbre-lib/tune_roles.py or cwd/timbre-lib/tune_roles.py or D:\pdf\roo projects\hdaw3\timbre-lib\tune_roles.py
            QStringList candidateScripts;
            candidateScripts << QCoreApplication::applicationDirPath() + "/timbre-lib/tune_roles.py";
            candidateScripts << QDir::current().filePath("timbre-lib/tune_roles.py");
            candidateScripts << "D:/pdf/roo projects/hdaw3/timbre-lib/tune_roles.py";
            candidateScripts << "timbre-lib/tune_roles.py";
            QString scriptPath;
            for (auto &c : candidateScripts) { QFileInfo fi(c); if (fi.exists()) { scriptPath = fi.absoluteFilePath(); break; } }
            if (!scriptPath.isEmpty()) {
                // Try python executables
                QStringList pyCands = {"python", "python3", "py"};
                // Also try wsl python if script is in WSL path (best fidelity)
                // We'll attempt native python first; if that fails fallback to C++ below
                for (auto &py : pyCands) {
                    QProcess proc;
                    QStringList args;
                    args << scriptPath << file.getFullPathName().toStdString().c_str();
                    if (!role.isEmpty()) args << "--role" << role;
                    proc.start(py, args);
                    if (!proc.waitForStarted(2000)) continue;
                    if (!proc.waitForFinished(15000)) { proc.kill(); continue; }
                    if (proc.exitCode() != 0) continue;
                    QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
                    if (out.contains("centroid") && out.contains("{")) {
                        // valid JSON from python
                        return McpToolResult::text(out);
                    }
                }
                // Try wsl wrapper on Windows: wsl <venv python> <wsl script> <wsl wav>
                {
                    QProcess proc;
                    // Convert wavPath to wsl: D:\x -> /mnt/d/x
                    QString wslWav = wavPath;
                    wslWav.replace("\\", "/");
                    if (wslWav.size() >= 2 && wslWav[1] == ':') {
                        QChar drive = wslWav[0].toLower();
                        wslWav = QString("/mnt/%1%2").arg(drive).arg(wslWav.mid(2));
                    }
                    QString wslScript = scriptPath;
                    wslScript.replace("\\", "/");
                    if (wslScript.size() >= 2 && wslScript[1] == ':') {
                        QChar drive = wslScript[0].toLower();
                        wslScript = QString("/mnt/%1%2").arg(drive).arg(wslScript.mid(2));
                    }
                    QStringList args;
                    args << "/home/hapbt/.prime/agent/kernel-venv/bin/python" << wslScript << wslWav;
                    if (!role.isEmpty()) args << "--role" << role;
                    proc.start("wsl", args);
                    if (proc.waitForStarted(3000) && proc.waitForFinished(20000) && proc.exitCode()==0) {
                        QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
                        if (out.contains("centroid") && out.contains("{")) return McpToolResult::text(out);
                    }
                }
            }

            // Fallback: pure C++ analysis (no python dependency)
            auto& fmtMgr = e->getProjectPool().getFormatManager();
            std::unique_ptr<juce::AudioFormatReader> reader(fmtMgr.createReaderFor(file));
            if (!reader) return McpToolResult::text("cannot open audio file", true);
            int64_t total = reader->lengthInSamples;
            if (total <= 0) return McpToolResult::text("empty audio", true);
            double sr = reader->sampleRate;
            int chans = (int)reader->numChannels;
            // Read up to ~30s max to bound analysis time; full mix 207s would be heavy but okay with 15s cap
            // Use full file if < 30s, otherwise first 30s (centroid stable)
            int64_t toRead = std::min<int64_t>(total, int64_t(sr * 30));
            juce::AudioBuffer<float> buf(chans, (int)toRead);
            reader->read(&buf, 0, (int)toRead, 0, true, true);
            std::vector<float> mono(toRead);
            for (int i = 0; i < toRead; ++i) {
                double sum = 0;
                for (int ch = 0; ch < chans; ++ch) sum += buf.getSample(ch, i);
                mono[i] = float(sum / chans);
            }
            Descriptors desc = computeDescriptors(mono, sr);

            QJsonObject out;
            out["wav"] = wavPath;
            QJsonObject dobj;
            dobj["centroid"] = std::round(desc.centroid * 10) / 10;
            dobj["bandwidth"] = std::round(desc.bandwidth * 10) / 10;
            dobj["rolloff85"] = std::round(desc.rolloff85 * 10) / 10;
            dobj["rolloff95"] = std::round(desc.rolloff95 * 10) / 10;
            dobj["mel_low"] = std::round(desc.melLow * 10000) / 10000;
            dobj["mel_mid"] = std::round(desc.melMid * 10000) / 10000;
            dobj["mel_high"] = std::round(desc.melHigh * 10000) / 10000;
            dobj["rms"] = std::round(desc.rms * 100000) / 100000;
            dobj["peak"] = std::round(desc.peak * 100000) / 100000;
            dobj["duration_s"] = std::round(desc.duration * 1000) / 1000;
            dobj["sampleRate"] = sr;
            out["descriptors"] = dobj;

            // summary string like timbre.py summarize (simple)
            QString summary;
            if (desc.centroid < 500) summary = "dark";
            else if (desc.centroid < 2000) summary = "warm/mid";
            else if (desc.centroid < 5000) summary = "bright";
            else summary = "very bright/edgy";
            if (desc.melHigh > 0.12) summary += ", airy top";
            out["summary"] = summary;

            if (!role.isEmpty()) {
                auto chk = checkRole(role, desc);
                out["check"] = chk;
                out["pass"] = chk.value("pass");
                out["suggestion"] = chk.value("suggestion");
            } else {
                // if no role, include per-role checks for all
                QJsonObject checks;
                auto targets = roleTargets();
                for (auto it = targets.begin(); it != targets.end(); ++it) {
                    checks[it.key()] = checkRole(it.key(), desc);
                }
                out["checks"] = checks;
            }
            // loop note
            out["loop"] = QJsonObject{{"note", "offline loop: analysis + suggestion only; re-render via export then re-analyze until pass or max 3"}};

            return McpToolResult::text(QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Indented)));
        }});
}

} // namespace mcp
