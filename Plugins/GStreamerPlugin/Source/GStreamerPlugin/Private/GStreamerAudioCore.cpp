#ifdef GError
#undef GError
#endif

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static bool ContainsNoCase(const char* Hay, const char* Needle)
{
    if (!Needle || !*Needle) return true;
    if (!Hay) return false;
    const size_t N = strlen(Needle);
    for (const char* P = Hay; *P; ++P)
    {
        size_t i = 0;
        while (i < N && P[i] && tolower((unsigned char)P[i]) == tolower((unsigned char)Needle[i])) ++i;
        if (i == N) return true;
    }
    return false;
}

extern "C" bool GStreamerFindAudioDeviceId(const char* device_class, const char* name_substring,
                                           const char* factory_name, char* out_id, int out_id_len,
                                           char* out_name, int out_name_len)
{
    GstDeviceMonitor* Mon = gst_device_monitor_new();
    gst_device_monitor_add_filter(Mon, device_class, nullptr);
    GList* Devices = gst_device_monitor_get_devices(Mon);

    bool bFound = false;
    for (GList* L = Devices; L && !bFound; L = L->next)
    {
        GstDevice* Dev  = GST_DEVICE(L->data);
        gchar*     Name = gst_device_get_display_name(Dev);
        if (Name && ContainsNoCase(Name, name_substring))
        {
            GstElement* E = gst_device_create_element(Dev, nullptr);
            if (E)
            {
                GstElementFactory* F = gst_element_get_factory(E);
                const char* FName = F ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(F)) : "";
                if ((!factory_name || strcmp(FName, factory_name) == 0) &&
                    g_object_class_find_property(G_OBJECT_GET_CLASS(E), "device"))
                {
                    gchar* Id = nullptr;
                    g_object_get(E, "device", &Id, nullptr);
                    if (Id && *Id)
                    {
                        if (out_id && out_id_len > 0)     snprintf(out_id, out_id_len, "%s", Id);
                        if (out_name && out_name_len > 0) snprintf(out_name, out_name_len, "%s", Name);
                        bFound = true;
                    }
                    g_free(Id);
                }
                gst_object_unref(E);
            }
        }
        g_free(Name);
    }
    g_list_free_full(Devices, gst_object_unref);
    gst_object_unref(Mon);
    return bFound;
}

extern "C" bool GStreamerAppSrcPush(void* appsrc, const unsigned char* data, int size,
                                    unsigned long long pts_ns, unsigned long long duration_ns)
{
    if (!appsrc) return false;
    GstBuffer* Buf = size > 0 ? gst_buffer_new_allocate(nullptr, size, nullptr) : gst_buffer_new();
    if (!Buf) return false;
    if (size > 0) gst_buffer_fill(Buf, 0, data, size);
    GST_BUFFER_PTS(Buf)      = pts_ns;
    GST_BUFFER_DURATION(Buf) = duration_ns;
    return gst_app_src_push_buffer(GST_APP_SRC(appsrc), Buf) == GST_FLOW_OK;
}

extern "C" bool GStreamerGetRunningTimeNs(void* pipeline, unsigned long long* out_ns)
{
    if (!pipeline || !out_ns) return false;
    GstClock* Clock = gst_element_get_clock(GST_ELEMENT(pipeline));
    if (!Clock) return false;
    const GstClockTime Now  = gst_clock_get_time(Clock);
    gst_object_unref(Clock);
    const GstClockTime Base = gst_element_get_base_time(GST_ELEMENT(pipeline));
    *out_ns = Now > Base ? Now - Base : 0;
    return true;
}

extern "C" void GStreamerUseSystemClock(void* pipeline)
{
    if (!pipeline) return;
    GstClock* Clock = gst_system_clock_obtain();
    gst_pipeline_use_clock(GST_PIPELINE(pipeline), Clock);
    gst_object_unref(Clock);
}
