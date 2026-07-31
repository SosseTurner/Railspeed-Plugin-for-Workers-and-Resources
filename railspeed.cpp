// railspeed - raise the hardcoded rail speed limits.
//
// Every infrastructure speed limit in the game comes out of one lookup
// function at SOVIET64.exe+3BCEB0. It returns a float, and each return is a
// rip-relative load of a pooled constant out of .rdata:
//
//     F3 0F 10 05 <disp32>        movss xmm0, [rip+disp32]
//
// The constants themselves cannot be edited. MSVC pools identical float
// literals, so the single .rdata slot holding 150.0f is shared by roughly 190
// unrelated call sites - gear ratios, camera zoom, UI geometry. Writing 230
// over it would corrupt all of them.
//
// So this plugin does not touch the constants. It allocates its own floats
// near the executable and rewrites only the 4-byte displacement of each load,
// so the same instruction reads a different number. Nothing else in the
// process can observe the change. That is technique 3 in docs/01-architecture:
// a data reference rewrite, no trampoline, no stolen bytes.
//
// The effective cap a train obeys is min(locomotive top speed, track limit),
// so raising a track limit does nothing until the locomotive's $ENGINE_SPEED
// clears it too. Wagons do not appear to constrain the train in practice.

#include "../../src/tesmio_plugin.h"

// ---------------------------------------------------------------- sites
//
// One entry per return in the lookup. `expect` is the whole 8-byte
// instruction as it ships in 1.1.1.7 and is checked before anything is
// written: a game update has to make this refuse, not corrupt the process.
//
// The 120 / 121 / 135 / 140 group are bridge and tunnel tiers. Which exact
// structure maps to which figure was established in game by changing one at a
// time, and 120 is shared by tunnels and one bridge type.

struct Site
{
    DWORD       insn;           // RVA of the movss
    float       stock;          // what it returns unpatched
    int         useMaster;      // does the `speed` key apply to it
    const char* key;            // per-tier override key in the ini
    const char* what;           // for the log
    BYTE        expect[8];
};

static Site g_sites[] =
{
    // NB: this site loads into xmm2, not xmm0 like the others - the ModRM
    // byte is 15, not 05. The patch only rewrites the displacement, so the
    // destination register is irrelevant to what we do; it only has to match
    // here so the guard recognises the instruction.
    { 0x3BCEB5, 120.0f, 1, "speed_120",      "tunnel + bridge (stock 120)",
      { 0xF3, 0x0F, 0x10, 0x15, 0x17, 0xDD, 0x54, 0x00 } },

    { 0x3BD159,  70.0f, 0, "speed_wood",     "wood rail       (stock  70)",
      { 0xF3, 0x0F, 0x10, 0x05, 0x6F, 0xD9, 0x54, 0x00 } },

    { 0x3BD16C, 150.0f, 1, "speed_concrete", "concrete rail   (stock 150)",
      { 0xF3, 0x0F, 0x10, 0x05, 0xC4, 0xDA, 0x54, 0x00 } },

    { 0x3BD19C, 121.0f, 1, "speed_121",      "bridge / tunnel (stock 121)",
      { 0xF3, 0x0F, 0x10, 0x05, 0x34, 0xDA, 0x54, 0x00 } },

    { 0x3BD1AA, 140.0f, 1, "speed_140",      "bridge / tunnel (stock 140)",
      { 0xF3, 0x0F, 0x10, 0x05, 0x62, 0xDA, 0x54, 0x00 } },

    { 0x3BD1BC, 135.0f, 1, "speed_135",      "bridge / tunnel (stock 135)",
      { 0xF3, 0x0F, 0x10, 0x05, 0x44, 0xDA, 0x54, 0x00 } },
};

static const int SITE_COUNT = (int)(sizeof(g_sites) / sizeof(g_sites[0]));

// Anything past this is refused. Not a game limit - a typo guard, so a stray
// zero cannot turn 230 into 2300 and make trains unstoppable.
static const float SPEED_MAX = 400.0f;

// ---------------------------------------------------------------- patching

static BYTE* g_pool;            // our own constants, from allocNear
static int   g_poolUsed;

static float* PoolFloat(float v)
{
    float* p = (float*)(g_pool + g_poolUsed);
    *p = v;
    g_poolUsed += sizeof(float);
    return p;
}

static bool PatchSite(const Site& s, float value)
{
    BYTE* insn = g_exeBase + s.insn;

    if (!H->readablePtr(insn, sizeof(s.expect)))
    {
        Logf("railspeed  %s: rva %08X not readable, skipped", s.what, s.insn);
        return false;
    }

    if (memcmp(insn, s.expect, sizeof(s.expect)) != 0)
    {
        Logf("railspeed  %s: rva %08X does not match 1.1.1.7, skipped", s.what, s.insn);
        Logf("railspeed    found %02X %02X %02X %02X %02X %02X %02X %02X",
             insn[0], insn[1], insn[2], insn[3], insn[4], insn[5], insn[6], insn[7]);
        return false;
    }

    float* slot  = PoolFloat(value);
    INT64  delta = (INT64)(BYTE*)slot - (INT64)(insn + 8);

    if (delta > 0x7FFFFFFFLL || delta < -0x7FFFFFFFLL)
    {
        Logf("railspeed  %s: constant %lld bytes away, out of rip range", s.what, delta);
        return false;
    }

    INT32 disp = (INT32)delta;
    DWORD prot = 0;

    if (!VirtualProtect(insn + 4, sizeof(disp), PAGE_EXECUTE_READWRITE, &prot))
    {
        Logf("railspeed  %s: VirtualProtect failed (%lu)", s.what, GetLastError());
        return false;
    }

    memcpy(insn + 4, &disp, sizeof(disp));
    VirtualProtect(insn + 4, sizeof(disp), prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), insn, sizeof(s.expect));

    Logf("railspeed  %s -> %.0f km/h", s.what, value);
    return true;
}

// ---------------------------------------------------------------- setup

extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    TsmBind(host);

    info->name    = "railspeed";
    info->version = "1.0";

    const char* ini = "plugins\\railspeed.ini";
    char        v[64];

    if (!H->configInt(ini, "railspeed", "enabled", 1))
    {
        Logf("railspeed  enabled = 0, nothing to do");
        return 1;
    }

    float master = 0.0f;
    if (H->configString(ini, "railspeed", "speed", v, sizeof(v), "") && v[0])
        master = (float)atof(v);

    g_pool = AllocNear(g_exeBase, 64);
    if (!g_pool)
    {
        Logf("railspeed  allocNear failed, cannot place constants");
        return 1;
    }

    int patched = 0;

    for (int i = 0; i < SITE_COUNT; ++i)
    {
        const Site& s = g_sites[i];

        float want = s.useMaster ? master : 0.0f;

        // A per-tier key always wins over the master, so one line can hold a
        // bridge back while everything else goes up.
        if (H->configString(ini, "railspeed", s.key, v, sizeof(v), "") && v[0])
        {
            double d = atof(v);
            if (d > 0.0) want = (float)d;
        }

        if (want <= 0.0f)      continue;      // not configured, leave stock
        if (want == s.stock)   continue;      // asked for what it already is

        if (want > SPEED_MAX)
        {
            Logf("railspeed  %s: %.0f refused, above the %.0f km/h sanity limit",
                 s.what, want, SPEED_MAX);
            continue;
        }

        if (PatchSite(s, want)) ++patched;
    }

    if (!patched)
    {
        Logf("railspeed  no limits changed, unloading");
        return 1;
    }

    Logf("railspeed  %d of %d limits patched", patched, SITE_COUNT);
    Logf("railspeed  remember a locomotive still obeys its own $ENGINE_SPEED");
    return 0;
}
