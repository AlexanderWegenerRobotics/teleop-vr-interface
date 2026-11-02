struct GRuler
{
    float Cov(float d, float w, float aa)
    {
        return 1.0 - smoothstep(w - aa, w + aa, d);
    }
    float Span(float x, float a, float b, float aa)
    {
        return saturate((x - a) / aa + 0.5) * saturate((b - x) / aa + 0.5);
    }
    float4 Over(float4 acc, float3 c, float a)
    {
        float na = a + acc.a * (1.0 - a);
        float3 rgb = (c * a + acc.rgb * acc.a * (1.0 - a)) / max(na, 1e-4);
        return float4(rgb, na);
    }
    float Seg(float2 p, float2 a, float2 b)
    {
        float2 pa = p - a;
        float2 ba = b - a;
        float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-9));
        return length(pa - ba * h);
    }
};
GRuler F;

float3 GOLD  = float3(1.00, 0.48, 0.02);
float3 GREEN = float3(0.02, 0.69, 0.15);
float3 CYAN  = float3(0.16, 0.72, 1.00);
float3 WHITE = float3(0.90, 0.92, 0.95);
float3 GREY  = float3(0.30, 0.31, 0.34);
float3 DARK  = float3(0.01, 0.012, 0.016);

float2 uv = UV;
int bits = (int)round(Geo.w);
if ((bits & 1) != 0) uv = uv.yx;
if ((bits & 2) != 0) uv.x = 1.0 - uv.x;
if ((bits & 4) != 0) uv.y = 1.0 - uv.y;

float L    = max(Geo.x, 1e-4);
float bf   = clamp(Geo.y, 0.05, 0.95);
float tape = Geo.z;
float Wm   = max(Style.z, 1e-4);
float Wq   = Wm / bf;

float s = tape > 0.5 ? (1.0 - uv.y) * L : uv.x * L;
float w = tape > 0.5 ? uv.x : uv.y;
float wm = w * Wq;
float aas = max(fwidth(s), 1e-6);
float aaw = max(fwidth(wm), 1e-6);
float aa  = max(aas, aaw);

float baseA = Style.x;
float act   = saturate(Style.y);
float onRuler = F.Span(s, 0.0, L, aas);
float inBand  = F.Span(wm, 0.0, Wm, aaw) * onRuler;

float4 acc = float4(0.0, 0.0, 0.0, 0.0);

if (Style.w > 0.5)
{
    acc = F.Over(acc, WHITE, baseA * 0.18 * inBand);
    acc = F.Over(acc, WHITE, baseA * onRuler * F.Cov(abs(wm), 0.6 * aaw, aaw));
    acc = F.Over(acc, WHITE, baseA * 0.6 * onRuler * F.Cov(abs(wm - Wm), 0.5 * aaw, aaw));
    float k = round(s / 0.01);
    float dk = abs(s - k * 0.01);
    float km = fmod(abs(k), 10.0);
    float k5 = fmod(abs(k), 5.0);
    float tl = km < 0.5 ? 0.80 : (k5 < 0.5 ? 0.55 : 0.30);
    float tw = km < 0.5 ? 0.8 : 0.5;
    acc = F.Over(acc, WHITE, baseA * onRuler * F.Cov(dk, tw * aas, aas) * step(wm, tl * Wm));
}

float bandsA[6] = { BandA.x, BandA.y, BandA.z, BandA.w, BandB.x, BandB.y };
float validAny = 0.0;
for (int j = 0; j < 3; ++j)
{
    if (bandsA[2 * j + 1] > bandsA[2 * j]) validAny = max(validAny, F.Span(s, bandsA[2 * j], bandsA[2 * j + 1], aas));
}

float exA = BandB.z;
float exB = BandB.w;
if (act > 0.001 && exB > exA)
{
    float inEx = F.Span(s, exA, exB, aas) * inBand * (1.0 - validAny);
    float hatch = step(0.5, frac((s + wm) / 0.006));
    acc = F.Over(acc, GREY, act * 0.55 * inEx);
    acc = F.Over(acc, DARK, act * 0.55 * inEx * hatch);
}

for (int i = 0; i < 3; ++i)
{
    float a0 = bandsA[2 * i];
    float b0 = bandsA[2 * i + 1];
    if (act > 0.001 && b0 > a0)
    {
        float inV = F.Span(s, a0, b0, aas);
        acc = F.Over(acc, GOLD, act * 0.32 * inV * inBand);
        acc = F.Over(acc, GOLD, act * 0.9 * inV * onRuler * F.Cov(abs(wm - Wm), 0.9 * aaw, aaw));
    }
}

float state = Target.w;
float3 tcol = state > 1.5 ? GREEN : GOLD;
if (act > 0.001 && state > 0.5)
{
    float c = Target.x;
    float t = max(Target.y, 0.0005);
    float bar = F.Span(s, c - t, c + t, aas) * F.Span(wm, -0.002, Wm * 1.12, aaw);
    acc = F.Over(acc, tcol, act * 0.6 * bar);
    float ends = min(abs(s - (c - t)), abs(s - (c + t)));
    acc = F.Over(acc, tcol, act * F.Cov(ends, 0.9 * aas, aas) * F.Span(wm, -0.002, Wm * 1.12, aaw));
}

float mk = Target.z;
if (act > 0.001)
{
    acc = F.Over(acc, CYAN, act * F.Cov(abs(s - mk), 1.1 * aas, aas) * F.Span(wm, -0.003, Wm * 1.08, aaw));
    float tri0 = Wm * 1.12;
    float triH = Wm * 0.70;
    float tl = saturate((wm - tri0) / triH);
    float inTri = step(tri0, wm) * step(wm, tri0 + triH) * step(abs(s - mk), tl * Wm * 0.55);
    acc = F.Over(acc, CYAN, act * 0.95 * inTri);
}

if (tape < 0.5 && Loupe.x > 0.5 && act > 0.001)
{
    float lv = saturate(Loupe.w) * act;
    float gain = max(Loupe.y, 1.0);
    float hr = max(Loupe.z, 0.001);
    float c = Target.x;
    float l0 = Wm * 2.05;
    float l1 = l0 + Wm;
    float r = (s - c) / gain;
    float inL = F.Span(s, c - hr * gain, c + hr * gain, aas) * F.Span(wm, l0, l1, aaw);
    acc = F.Over(acc, DARK, lv * 0.7 * inL);
    float k = round(r / 0.002);
    float dk = abs(r - k * 0.002) * gain;
    float major = fmod(abs(k), 5.0) < 0.5 ? 1.0 : 0.0;
    float tlen = l0 + Wm * (major > 0.5 ? 0.7 : 0.38);
    acc = F.Over(acc, WHITE, lv * 0.9 * inL * F.Cov(dk, (major > 0.5 ? 0.8 : 0.5) * aas, aas) * step(wm, tlen));
    float t = max(Target.y, 0.0005);
    acc = F.Over(acc, tcol, lv * 0.45 * inL * F.Span(r, -t, t, aas / gain));
    float rm = clamp((mk - c) / gain, -hr, hr);
    acc = F.Over(acc, CYAN, lv * inL * F.Cov(abs(r - rm) * gain, 1.3 * aas, aas));
    float edge = min(abs(wm - l0), abs(wm - l1));
    acc = F.Over(acc, WHITE, lv * 0.8 * F.Span(s, c - hr * gain, c + hr * gain, aas) * F.Cov(edge, 0.5 * aaw, aaw));
}

if (tape > 0.5 && state > 2.5 && act > 0.001)
{
    float rr = Wm * 0.75;
    float2 p = float2(wm - (Wm * 1.12 + Wm * 0.70 + rr * 1.3), s - mk);
    float disc = F.Cov(length(p), rr, aa);
    acc = F.Over(acc, GREEN, act * disc);
    float ck = min(F.Seg(p / rr, float2(-0.45, 0.0), float2(-0.1, -0.38)), F.Seg(p / rr, float2(-0.1, -0.38), float2(0.5, 0.35)));
    acc = F.Over(acc, DARK, act * disc * F.Cov(ck * rr, 0.09 * rr, aa));
}

return acc;
