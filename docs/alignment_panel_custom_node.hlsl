struct APanel
{
    float Seg(float2 p, float2 a, float2 b)
    {
        float2 pa = p - a;
        float2 ba = b - a;
        float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-6));
        return length(pa - ba * h);
    }
    float Cov(float d, float w, float aa)
    {
        return 1.0 - smoothstep(w - aa, w + aa, d);
    }
    float4 Over(float4 acc, float3 c, float a)
    {
        float na = a + acc.a * (1.0 - a);
        float3 rgb = (c * a + acc.rgb * acc.a * (1.0 - a)) / max(na, 1e-4);
        return float4(rgb, na);
    }
    float4 Ink(float4 acc, float3 c, float a, float d, float w, float aa)
    {
        acc = Over(acc, float3(0.0, 0.0, 0.0), 0.6 * a * Cov(d, w + 0.03, aa));
        return Over(acc, c, a * Cov(d, w, aa));
    }
    float2 Dir(float2 v)
    {
        float l = length(v);
        return l > 1e-5 ? v / l : float2(0.0, 1.0);
    }
};
APanel F;

float2 p = float2(UV.x * 2.4 - 1.05, (1.0 - UV.y) * 2.4 - 1.35);
float aa = max(max(fwidth(p.x), fwidth(p.y)), 1e-4) * 1.2;

float3 GOLD  = float3(1.00, 0.48, 0.02);
float3 GREEN = float3(0.02, 0.69, 0.15);
float3 WARNC = float3(1.00, 0.09, 0.01);
float3 WHITE = float3(0.90, 0.92, 0.95);

float xyOk   = Flags.x;
float yawOk  = Flags.y;
float tiltOk = Flags.z;
float warn   = Flags.w;
float done   = ZD.w;
float good   = max(done, xyOk);

float4 acc = float4(0.0, 0.0, 0.0, 0.0);

float r = length(p);
float hair = min(F.Seg(p, float2(-1.0, 0.0), float2(1.0, 0.0)), F.Seg(p, float2(0.0, -1.0), float2(0.0, 1.0)));
acc = F.Ink(acc, WHITE, 0.35, hair, 0.007, aa);
acc = F.Ink(acc, WHITE, 0.30, abs(r - 1.0), 0.007, aa);

float rt = XY.z;
acc = F.Over(acc, GREEN, good * lerp(0.15, 0.4, done) * F.Cov(r - rt, 0.0, aa));
acc = F.Ink(acc, lerp(WHITE, GREEN, good), 0.95, abs(r - rt), 0.018, aa);

float2 td = F.Dir(Yaw.xy);
acc = F.Ink(acc, WHITE, 0.8, F.Seg(p, -td * 0.6, td * 0.6), 0.014, aa);

float2 dp = XY.xy;
float dl = length(dp);
dp = dl > 1.0 ? dp / dl : dp;

float2 cp = Cmd.xy;
float cl = length(cp);
cp = cl > 1.0 ? cp / cl : cp;
float show = saturate(Cmd.z);
acc = F.Ink(acc, WHITE, 0.55 * show, F.Seg(p, dp, cp), 0.006, aa);
acc = F.Ink(acc, WHITE, show, abs(length(p - cp) - 0.08), 0.016, aa);

float2 sd = F.Dir(Yaw.zw);
float3 yawC = lerp(GOLD, GREEN, max(yawOk, done));
acc = F.Ink(acc, yawC, 1.0, F.Seg(p, dp - sd * 0.45, dp + sd * 0.45), 0.026, aa);

float2 q = p - dp;
float dd = length(q);
float ring = ZD.x;
float3 ringC = (warn > 0.5 && warn < 1.5) ? WARNC : GOLD;
acc = F.Ink(acc, ringC, step(0.001, ring) * (1.0 - done), abs(dd - ring), 0.02, aa);

float depth = ZD.y;
float a01 = frac(atan2(q.x, q.y) / 6.2831853 + 1.0);
float inArc = step(a01, depth) * step(0.001, depth) * (1.0 - done);
float3 depC = warn > 1.5 ? WARNC : GREEN;
acc = F.Ink(acc, depC, inArc, abs(dd - 0.17), 0.028, aa);

acc = F.Ink(acc, lerp(GOLD, GREEN, good), 1.0, dd - 0.065, 0.0, aa);

float te = Tilt.w;
float band = Tilt.z;
float3 tC = warn > 1.5 ? WARNC : lerp(GOLD, GREEN, max(tiltOk, done));
float gx = 1.2;
acc = F.Ink(acc, WHITE, 0.45 * te, F.Seg(p, float2(gx, -1.0), float2(gx, 1.0)), 0.008, aa);
acc = F.Ink(acc, GREEN, 0.6 * te, F.Seg(p, float2(gx, -band), float2(gx, band)), 0.03, aa);
acc = F.Ink(acc, tC, te, length(p - float2(gx, clamp(Tilt.x, -1.0, 1.0))) - 0.07, 0.0, aa);
float gy = -1.2;
acc = F.Ink(acc, WHITE, 0.45 * te, F.Seg(p, float2(-1.0, gy), float2(1.0, gy)), 0.008, aa);
acc = F.Ink(acc, GREEN, 0.6 * te, F.Seg(p, float2(-band, gy), float2(band, gy)), 0.03, aa);
acc = F.Ink(acc, tC, te, length(p - float2(clamp(Tilt.y, -1.0, 1.0), gy)) - 0.07, 0.0, aa);

return float4(acc.rgb, acc.a * saturate(XY.w));
