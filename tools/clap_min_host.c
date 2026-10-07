// clap_min_host.c — minimal standalone CLAP host, HDAW's diagnostic for
// "this plugin renders silence" investigations.
//
// WHY THIS EXISTS (2026-10-07). `McpServer.ExportAudioWithClapPluginDoesNotHang`
// failed on the Linux box: an isolated CLAP slot published 775 params yet the
// exported WAV was all zeros. HDAW's own logs were not enough to tell whether
// the host or the plugin was at fault, so this host was written: no JUCE, no
// HDAW, ~340 lines of C against clap/clap.h. It renders ONE plugin for ~1 s
// with a single note and prints the output peak — plus, crucially, the return
// value of `clap_plugin::process`.
//
// That is how the bug was found. `HDAW::CLAPPluginInstance` used to collapse
// EVERY one of a plugin's output ports into ONE host port whose channel count
// was the SUM of all of them (`buildBuses`/`processBlock`). Surge XT declares
// three output ports (Output/Scene A/Scene B, 2ch each); handed a single
// 6-channel port it returns CLAP_PROCESS_ERROR (=0) and writes nothing —
// silently, because the status was discarded. This host reproduces it in one
// command:
//
//   build:  gcc -O0 -g -std=c11 \
//             -I<repo>/build/clap-juce-extensions-src/clap-libs/clap/include \
//             -o /tmp/clapmin tools/clap_min_host.c -ldl -lm
//
//   # HDAW's old layout: ONE summed output port (6ch for Surge XT)
//   MINHOST_OUTCH=6        /tmp/clapmin "$HOME/.clap/Surge XT.clap"   # status 0, PEAK 0.000
//   # HDAW's fixed layout: one host port per plugin port
//   MINHOST_MULTIPORT=1    /tmp/clapmin "$HOME/.clap/Surge XT.clap"   # status 1, PEAK 0.449
//
// Env knobs:
//   MINHOST_OUTCH=<n>      channels declared on the single summed port (old shape)
//   MINHOST_MULTIPORT=1    one host port per plugin port (new shape)
//   MINHOST_INPUT=1        also declare a 2ch input port
//   MINHOST_STEADY_NEG1=1  pass steady_time = -1
//   MINHOST_FLUSH=1        call params->flush(p, NULL, NULL) before activate
//                          (SEGFAULTS against Surge XT — the null event lists
//                          are illegal per the CLAP spec; see the
//                          EmptyInputEvents/EmptyOutputEvents guard in
//                          CLAPPluginInstance.cpp)
//   MINHOST_SUMMARY=<file> write "PEAK=.. RMS=.. blocks=.. ports=.. status=.."
//                          to <file> — REQUIRED for plugins that flood stdout
//                          (NodalRed2x emits 300 KB+ of emulator logs, which
//                          corrupts line-oriented capture)
//
// scripts/clap_port_matrix.py drives this over every installed .clap and prints
// the old-vs-new table. The HERMETIC regression net for the port shape is
// `tests/unit/engine/clap_port_layout_test.cpp` (suite `CLAPPortLayout`) — that
// one uses a stub plugin and needs no real plugins installed.
// Minimal CLAP host — the decisive control experiment for
// McpServer.ExportAudioWithClapPluginDoesNotHang.
//
// Loads ONE .clap plugin, activates it at 44100/512, sends a note-on, renders
// ~1 s, then prints the output peak plus a few param values. No JUCE, no HDAW:
// if a plugin is silent HERE the silence is the plugin's (or its patch's),
// not HDAW's host implementation.
//
// build: gcc -O0 -g -std=c11 -o /tmp/clapmin /tmp/clap_min_host.c -ldl -lm
#include <clap/clap.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static const char* gPath = NULL;

static bool hostIsMainThread(const clap_host_t* h) { (void) h; return true; }
static void hostLog(const clap_host_t* h, clap_log_severity sev, const char* msg)
{
    (void) h;
    fprintf(stderr, "[plugin-log sev=%d] %s\n", (int) sev, msg);
}
static void hostRescan(const clap_host_t* h, clap_param_rescan_flags f) { (void) h; (void) f; }
static void hostClear(const clap_host_t* h, clap_id id, clap_param_clear_flags f) { (void) h; (void) id; (void) f; }
static void hostRequestRestart(const clap_host_t* h) { (void) h; }
static void hostRequestProcess(const clap_host_t* h) { (void) h; }
static void hostRequestCallback(const clap_host_t* h) { (void) h; }
static bool hostIsRescanFlagSupported(const clap_host_t* h, uint32_t f) { (void) h; (void) f; return false; }
static void hostRescanFlags(const clap_host_t* h, uint32_t f) { (void) h; (void) f; }
static uint32_t hostSupportedDialects(const clap_host_t* h) { (void) h; return 1u << 0; }

static clap_host_t gHost;

static const void* hostGetExtension(const clap_host_t* h, const char* id)
{
    (void) h;
    if (!strcmp(id, CLAP_EXT_LOG)) {
        static clap_host_log_t x; x.log = hostLog; return &x;
    }
    if (!strcmp(id, CLAP_EXT_THREAD_CHECK)) {
        static clap_host_thread_check_t x;
        x.is_main_thread = hostIsMainThread;
        x.is_audio_thread = hostIsMainThread;
        return &x;
    }
    if (!strcmp(id, CLAP_EXT_PARAMS)) {
        static clap_host_params_t x;
        x.rescan = hostRescan; x.clear = hostClear;
        return &x;
    }
    if (!strcmp(id, CLAP_EXT_LATENCY)) {
        static clap_host_latency_t x; x.changed = hostRequestRestart; return &x;
    }
    if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) {
        static clap_host_audio_ports_t x;
        x.is_rescan_flag_supported = hostIsRescanFlagSupported;
        x.rescan = hostRescanFlags;
        return &x;
    }
    if (!strcmp(id, CLAP_EXT_NOTE_PORTS)) {
        static clap_host_note_ports_t x;
        x.supported_dialects = hostSupportedDialects;
        x.rescan = hostRescanFlags;
        return &x;
    }
    if (!strcmp(id, CLAP_EXT_STATE)) {
        static clap_host_state_t x; x.mark_dirty = hostRequestCallback; return &x;
    }
    return NULL;
}

/* ── a one-shot event list holding at most a few events ─────────────────── */
#define MAXEV 16
typedef struct { const clap_event_header_t* ev[MAXEV]; uint32_t n; } EvList;

static uint32_t inSize(const clap_input_events_t* l) { return ((EvList*) l->ctx)->n; }
static const clap_event_header_t* inGet(const clap_input_events_t* l, uint32_t i)
{
    EvList* e = (EvList*) l->ctx;
    return i < e->n ? e->ev[i] : NULL;
}

static bool outPush(const clap_output_events_t* l, const clap_event_header_t* e)
{ (void) l; (void) e; return true; }

/* ── state capture helper (mirrors HDAW's proxy state.save) ─────────────── */
static unsigned char gStore[1 << 20];
static uint64_t gStoreN = 0;
static int64_t storeWrite(const clap_ostream_t* s, const void* b, uint64_t n)
{
    (void) s;
    if (gStoreN + n > sizeof(gStore)) n = sizeof(gStore) - gStoreN;
    memcpy(gStore + gStoreN, b, (size_t) n);
    gStoreN += n;
    return (int64_t) n;
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <plugin.clap>\n", argv[0]); return 2; }
    gPath = argv[1];

    void* lib = dlopen(gPath, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 3; }

    const clap_plugin_entry_t* entry = (const clap_plugin_entry_t*) dlsym(lib, "clap_entry");
    if (!entry) { fprintf(stderr, "no clap_entry symbol\n"); return 3; }
    if (!entry->init(gPath)) { fprintf(stderr, "entry->init failed\n"); return 3; }

    const clap_plugin_factory_t* fac =
        (const clap_plugin_factory_t*) entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!fac) { fprintf(stderr, "no plugin factory\n"); return 3; }
    printf("plugin count: %u\n", fac->get_plugin_count(fac));

    const clap_plugin_descriptor_t* d0 = fac->get_plugin_descriptor(fac, 0);
    printf("desc[0]: id=%s name=%s\n", d0->id, d0->name);

    memset(&gHost, 0, sizeof(gHost));
    gHost.clap_version.major = CLAP_VERSION_MAJOR;
    gHost.clap_version.minor = CLAP_VERSION_MINOR;
    gHost.clap_version.revision = CLAP_VERSION_REVISION;
    gHost.name = "clapmin";
    gHost.vendor = "hdaw";
    gHost.url = "";
    gHost.version = "0.1";
    gHost.get_extension = hostGetExtension;
    gHost.request_restart = hostRequestRestart;
    gHost.request_process = hostRequestProcess;
    gHost.request_callback = hostRequestCallback;

    const clap_plugin_t* p = fac->create_plugin(fac, &gHost, d0->id);
    if (!p) { fprintf(stderr, "create_plugin failed\n"); return 3; }
    printf("plugin->init: %d\n", p->init(p) ? 1 : 0);

    const clap_plugin_params_t* params =
        (const clap_plugin_params_t*) p->get_extension(p, CLAP_EXT_PARAMS);
    if (params) {
        const uint32_t n = params->count(p);
        printf("param count: %u\n", n);
        int shown = 0;
        for (uint32_t i = 0; i < n && shown < 12; ++i) {
            clap_param_info_t info;
            memset(&info, 0, sizeof(info));
            if (!params->get_info(p, i, &info)) continue;
            if (!strstr(info.name, "Volume") && !strstr(info.name, "Osc 1 Type")) continue;
            double v = 0.0;
            const bool ok = params->get_value(p, info.id, &v);
            printf("  [%u] id=%u '%s' value=%.6f get=%d range=[%.3f,%.3f]\n",
                   i, info.id, info.name, v, ok ? 1 : 0, info.min_value, info.max_value);
            ++shown;
        }
    }

    /* dump the plugin's own state the way HDAW's proxy does */
    const clap_plugin_state_t* st =
        (const clap_plugin_state_t*) p->get_extension(p, CLAP_EXT_STATE);
    if (st) {
        clap_ostream_t os;
        memset(&os, 0, sizeof(os));
        os.write = storeWrite;
        const bool ok = st->save(p, &os);
        printf("state save: ok=%d bytes=%llu\n", ok ? 1 : 0, (unsigned long long) gStoreN);
        const char* txt = (const char*) memchr(gStore, '<', (size_t) gStoreN);
        if (txt) {
            const char* keys[4] = { "<volume ", "<a_vca_level ", "<a_level_o1 ", "<a_env1_sustain " };
            const char* endp = (const char*) gStore + gStoreN;
            for (int k = 0; k < 4; ++k) {
                const char* q = strstr(txt, keys[k]);
                if (q && q < endp) {
                    char buf[80];
                    size_t i = 0;
                    while (q + i < endp && q[i] && i < 79) { buf[i] = q[i]; ++i; }
                    buf[i] = 0;
                    printf("  state has: %s\n", buf);
                }
            }
        }
    }

    const double sr = 44100.0;
    const uint32_t bs = 512;

    /* optional: replicate HDAW's params->flush(plugin, NULL, NULL) */
    if (params && getenv("MINHOST_FLUSH") && getenv("MINHOST_FLUSH")[0] == '1') {
        params->flush(p, NULL, NULL);
        printf("called params->flush(p, NULL, NULL)\n");
    }

    printf("activate: %d\n", p->activate(p, sr, 1, bs) ? 1 : 0);
    printf("start_processing: %d\n", p->start_processing(p) ? 1 : 0);

    const clap_plugin_audio_ports_t* ap =
        (const clap_plugin_audio_ports_t*) p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
    uint32_t nOutPorts = 0, nInPorts = 0, sumOutCh = 0, sumInCh = 0, p0OutCh = 0;
    if (ap) {
        nInPorts = ap->count(p, true);
        nOutPorts = ap->count(p, false);
        printf("audio ports: in=%u out=%u\n", nInPorts, nOutPorts);
        for (uint32_t i = 0; i < nInPorts; ++i) {
            clap_audio_port_info_t info; memset(&info, 0, sizeof(info));
            if (ap->get(p, i, true, &info)) {
                printf("  IN [%u] '%s' ch=%u\n", i, info.name, info.channel_count);
                sumInCh += info.channel_count;
            }
        }
        for (uint32_t i = 0; i < nOutPorts; ++i) {
            clap_audio_port_info_t info; memset(&info, 0, sizeof(info));
            if (ap->get(p, i, false, &info)) {
                printf("  OUT[%u] '%s' ch=%u\n", i, info.name, info.channel_count);
                sumOutCh += info.channel_count;
                if (i == 0) p0OutCh = info.channel_count;
            }
        }
        printf("  sums: out_ch=%u first_out_ch=%u in_ch=%u\n", sumOutCh, p0OutCh, sumInCh);
    }

    enum { CH = 6 };
    float* inbufs[2];
    inbufs[0] = (float*) calloc(bs, sizeof(float));
    inbufs[1] = (float*) calloc(bs, sizeof(float));
    clap_audio_buffer_t inb;
    memset(&inb, 0, sizeof(inb));
    inb.data32 = inbufs;
    inb.channel_count = 2;
    const char* wantIn = getenv("MINHOST_INPUT");
    const bool declareInput = (wantIn && wantIn[0] == '1');
    printf("declare input port: %d\n", declareInput ? 1 : 0);

    float* bufs[CH];
    for (int c = 0; c < CH; ++c) bufs[c] = (float*) calloc(bs, sizeof(float));
    /* MINHOST_OUTCH: how many channels we DECLARE on the single output port
       (allocated = 6). HDAW declares min(sum_of_all_output_port_channels,
       host_buffer_channels); Surge has 3 x 2ch output ports => 6. */
    const char* oc = getenv("MINHOST_OUTCH");
    const uint32_t declaredOutCh = oc ? (uint32_t) atoi(oc) : (uint32_t) CH;
    clap_audio_buffer_t out;
    memset(&out, 0, sizeof(out));
    out.data32 = bufs;
    out.channel_count = declaredOutCh;
    printf("declared out channels: %u (allocated %d)\n", declaredOutCh, (int) CH);

    clap_input_events_t in;
    clap_output_events_t oue;
    memset(&in, 0, sizeof(in));
    memset(&oue, 0, sizeof(oue));
    in.size = inSize; in.get = inGet;
    oue.try_push = outPush;

    clap_event_note_t note;
    memset(&note, 0, sizeof(note));
    note.header.size = sizeof(note);
    note.header.type = CLAP_EVENT_NOTE_ON;
    note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    note.port_index = 0;
    note.channel = 0;
    note.key = 60;
    note.velocity = 0.8;

    const char* mpEnv = getenv("MINHOST_MULTIPORT");
    const bool multiport = (mpEnv && mpEnv[0] == '1');
    printf("multiport mode: %d\n", multiport ? 1 : 0);
    const uint32_t NB = (uint32_t)(sr / bs);
    int block0Status = -99;
    double peak = 0.0, sumSq = 0.0;
    long nsum = 0;
    bool sent = false;

    for (uint32_t b = 0; b < NB; ++b) {
        for (int c = 0; c < CH; ++c) memset(bufs[c], 0, bs * sizeof(float));

        EvList el;
        el.n = 0;
        if (!sent) { el.ev[el.n++] = &note.header; sent = true; }
        in.ctx = &el;

        clap_process_t proc;
        memset(&proc, 0, sizeof(proc));
        if (declareInput) {
            proc.audio_inputs = &inb;
            proc.audio_inputs_count = 1;
        }
        clap_audio_buffer_t outs[8];
        memset(outs, 0, sizeof(outs));
        if (multiport) {
            for (uint32_t q = 0; q < nOutPorts && q < 8; ++q) {
                clap_audio_port_info_t info; memset(&info, 0, sizeof(info));
                if (!ap || !ap->get(p, q, false, &info)) continue;
                const uint32_t cc = info.channel_count < 2 ? info.channel_count : 2u;
                outs[q].data32 = &bufs[q * 2];
                outs[q].channel_count = cc;
            }
            proc.audio_outputs = outs;
            proc.audio_outputs_count = nOutPorts;
        }
        proc.steady_time = (getenv("MINHOST_STEADY_NEG1") && getenv("MINHOST_STEADY_NEG1")[0] == '1')
                               ? -1 : (int64_t) b * (int64_t) bs;
        proc.frames_count = bs;
        if (!multiport) {
            proc.audio_outputs = &out;
            proc.audio_outputs_count = 1;
        }
        proc.in_events = &in;
        proc.out_events = &oue;

        const clap_process_status st = p->process(p, &proc);
        if (b == 0) { printf("block0 status=%d\n", (int) st); block0Status = (int) st; }

        for (int c = 0; c < CH; ++c)
            for (uint32_t i = 0; i < bs; ++i) {
                const double v = fabs((double) bufs[c][i]);
                if (v > peak) peak = v;
                sumSq += v * v;
                ++nsum;
            }
    }

    printf("PEAK=%.6f RMS=%.6f blocks=%u\n", peak, sqrt(sumSq / (nsum ? (double) nsum : 1.0)), NB);

    /* Some plugins (NodalRed2x) flood stdout with 300 KB+ of emulator logs,
       which makes line-oriented capture unreliable. Write the summary to a
       separate file too when asked. */
    if (const char* summ = getenv("MINHOST_SUMMARY")) {
        if (FILE* sf = fopen(summ, "w")) {
            fprintf(sf, "PEAK=%.6f RMS=%.6f blocks=%u ports=%u status=%d\n",
                    peak, sqrt(sumSq / (nsum ? (double) nsum : 1.0)), NB,
                    nOutPorts, (int) block0Status);
            fclose(sf);
        }
    }

    p->stop_processing(p);
    p->deactivate(p);
    p->destroy(p);
    entry->deinit();
    dlclose(lib);
    return (peak > 0.001) ? 0 : 1;
}
