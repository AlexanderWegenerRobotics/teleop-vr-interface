#ifdef GError
#undef GError
#endif

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern "C" void GStreamerInit()
{
    gst_init(nullptr, nullptr);
    printf("GStreamer initialized\n");
}

extern "C" void GStreamerDeinit()
{
    gst_deinit();
    printf("GStreamer deinitialized\n");
}

extern "C" void* GStreamerCreatePipeline(const char* description)
{
    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(description, &error);

    if (error) {
        printf("Pipeline error: %s\n", error->message);
        g_error_free(error);
        return nullptr;
    }

    return pipeline;
}

extern "C" bool GStreamerStartPipeline(void* pipeline)
{
    if (!pipeline) return false;

    GstStateChangeReturn ret = gst_element_set_state(
        GST_ELEMENT(pipeline),
        GST_STATE_PLAYING
    );

    return (ret != GST_STATE_CHANGE_FAILURE);
}

// Blocks until the pipeline actually reaches PLAYING (or fails/times out) — catches
// decoders that accept the state change asynchronously then fail to negotiate.
//
// NOTE: our receive pipelines are driven by udpsrc, which is a *live* source, so they
// never preroll. GStreamer signals that with GST_STATE_CHANGE_NO_PREROLL, which is a
// success return for live pipelines -- not a failure. Treating it as failure (as this
// did originally) made the nvh264dec path look broken 100% of the time: every Start()
// burned the full timeout here and then fell back to CPU decode. Accept both returns
// and let `state` be the real verdict.
extern "C" bool GStreamerWaitForPlaying(void* pipeline, int timeout_ms)
{
    if (!pipeline) return false;
    GstState state = GST_STATE_NULL, pending = GST_STATE_NULL;
    GstStateChangeReturn ret = gst_element_get_state(
        GST_ELEMENT(pipeline), &state, &pending,
        (GstClockTime)timeout_ms * GST_MSECOND);

    const bool bResolved = (ret == GST_STATE_CHANGE_SUCCESS) ||
                           (ret == GST_STATE_CHANGE_NO_PREROLL);
    return bResolved && state == GST_STATE_PLAYING;
}

extern "C" void GStreamerStopPipeline(void* pipeline)
{
    if (pipeline) {
        gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
    }
}

extern "C" void GStreamerDestroyPipeline(void* pipeline)
{
    if (pipeline) {
        gst_object_unref(GST_OBJECT(pipeline));
    }
}

extern "C" void* GStreamerGetElementByName(void* pipeline, const char* name)
{
    if (!pipeline) return nullptr;
    return gst_bin_get_by_name(GST_BIN(pipeline), name);
}

extern "C" void* GStreamerPullSample(void* appsink)
{
    if (!appsink) return nullptr;
    return gst_app_sink_pull_sample(GST_APP_SINK(appsink));
}

extern "C" void* GStreamerTryPullSample(void* appsink, double timeout_seconds)
{
    if (!appsink) return nullptr;
    GstClockTime timeout_ns = (GstClockTime)(timeout_seconds * GST_SECOND);
    return gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), timeout_ns);
}

extern "C" bool GStreamerIsEOS(void* appsink)
{
    if (!appsink) return true;
    return gst_app_sink_is_eos(GST_APP_SINK(appsink));
}

extern "C" void* GStreamerGetSampleBuffer(void* sample)
{
    if (!sample) return nullptr;
    return gst_sample_get_buffer(GST_SAMPLE(sample));
}

extern "C" void* GStreamerGetSampleCaps(void* sample)
{
    if (!sample) return nullptr;
    return gst_sample_get_caps(GST_SAMPLE(sample));
}

extern "C" bool GStreamerGetVideoDimensions(void* caps, int* width, int* height)
{
    if (!caps) return false;

    GstStructure* structure = gst_caps_get_structure(GST_CAPS(caps), 0);
    if (!structure) return false;

    return gst_structure_get_int(structure, "width", width) &&
        gst_structure_get_int(structure, "height", height);
}

extern "C" bool GStreamerCopyBufferData(void* buffer, unsigned char* dest, int size)
{
    if (!buffer || !dest) return false;

    GstMapInfo map;
    if (!gst_buffer_map(GST_BUFFER(buffer), &map, GST_MAP_READ)) {
        return false;
    }

    int copy_size = (map.size < (gsize)size) ? map.size : size;
    memcpy(dest, map.data, copy_size);

    gst_buffer_unmap(GST_BUFFER(buffer), &map);
    return true;
}

extern "C" int GStreamerGetBufferSize(void* buffer)
{
    if (!buffer) return 0;
    return (int)gst_buffer_get_size(GST_BUFFER(buffer));
}

extern "C" void GStreamerFreeSample(void* sample)
{
    if (sample) {
        gst_sample_unref(GST_SAMPLE(sample));
    }
}

extern "C" void GStreamerUnrefElement(void* element)
{
    if (element) {
        gst_object_unref(GST_OBJECT(element));
    }
}

extern "C" void* GStreamerGetBus(void* pipeline)
{
    if (!pipeline) return nullptr;
    return gst_element_get_bus(GST_ELEMENT(pipeline));
}

extern "C" void* GStreamerPollBusMessage(void* bus, double timeout_seconds)
{
    if (!bus) return nullptr;
    return gst_bus_timed_pop(GST_BUS(bus), (GstClockTime)(timeout_seconds * GST_SECOND));
}

extern "C" int GStreamerGetMessageType(void* message)
{
    if (!message) return 0;
    return GST_MESSAGE_TYPE(GST_MESSAGE(message));
}

extern "C" void GStreamerFreeMessage(void* message)
{
    if (message) {
        gst_message_unref(GST_MESSAGE(message));
    }
}

extern "C" void GStreamerUnrefBus(void* bus)
{
    if (bus) {
        gst_object_unref(GST_OBJECT(bus));
    }
}

// ---------------------------------------------------------------------------
// RTP pad probe — pre-FEC, on rtpulpfecdec sink pad
// Gives raw sequence numbers before FEC recovery, for true loss + jitter
// ---------------------------------------------------------------------------

struct FRtpProbeData {
    void (*callback)(uint16_t seq, uint32_t timestamp, void* userdata);
    void* userdata;
};

static GstPadProbeReturn RtpProbeCallback(
    GstPad* pad, GstPadProbeInfo* info, gpointer user_data)
{
    FRtpProbeData* data = static_cast<FRtpProbeData*>(user_data);

    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buffer) return GST_PAD_PROBE_OK;

    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    if (!gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp))
        return GST_PAD_PROBE_OK;

    uint16_t seq = gst_rtp_buffer_get_seq(&rtp);
    uint32_t ts  = gst_rtp_buffer_get_timestamp(&rtp);

    gst_rtp_buffer_unmap(&rtp);

    if (data->callback)
        data->callback(seq, ts, data->userdata);

    return GST_PAD_PROBE_OK;
}

static void FreeRtpProbeData(gpointer data)
{
    delete static_cast<FRtpProbeData*>(data);
}

/**
 * Attach a probe on the sink pad of the named element.
 * For pre-FEC loss: attach to "fecdec" sink pad (sees all arriving RTP packets).
 * The callback receives (seq, rtp_timestamp, userdata) for every packet.
 * Returns true on success.
 */
extern "C" bool GStreamerAddRtpProbe(
    void* pipeline,
    const char* element_name,
    void (*callback)(uint16_t seq, uint32_t timestamp, void* userdata),
    void* userdata)
{
    if (!pipeline || !callback) return false;

    GstElement* element = gst_bin_get_by_name(GST_BIN(pipeline), element_name);
    if (!element) {
        printf("[GStreamer] GStreamerAddRtpProbe: element '%s' not found\n", element_name);
        return false;
    }

    GstPad* pad = gst_element_get_static_pad(element, "sink");
    if (!pad) {
        printf("[GStreamer] GStreamerAddRtpProbe: no sink pad on '%s'\n", element_name);
        gst_object_unref(element);
        return false;
    }

    FRtpProbeData* data = new FRtpProbeData{ callback, userdata };

    gst_pad_add_probe(
        pad,
        GST_PAD_PROBE_TYPE_BUFFER,
        RtpProbeCallback,
        data,
        FreeRtpProbeData
    );

    gst_object_unref(pad);
    gst_object_unref(element);

    printf("[GStreamer] RTP probe attached to '%s' sink pad\n", element_name);
    return true;
}

// ---------------------------------------------------------------------------
// FEC recovery probe — on rtpulpfecdec src pad
// Counts packets that passed through after FEC (post-FEC loss = gaps here)
// ---------------------------------------------------------------------------

struct FFecProbeData {
    void (*callback)(uint16_t seq, void* userdata);
    void* userdata;
};

static GstPadProbeReturn FecProbeCallback(
    GstPad* pad, GstPadProbeInfo* info, gpointer user_data)
{
    FFecProbeData* data = static_cast<FFecProbeData*>(user_data);

    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buffer) return GST_PAD_PROBE_OK;

    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    if (!gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp))
        return GST_PAD_PROBE_OK;

    uint16_t seq = gst_rtp_buffer_get_seq(&rtp);
    gst_rtp_buffer_unmap(&rtp);

    if (data->callback)
        data->callback(seq, data->userdata);

    return GST_PAD_PROBE_OK;
}

static void FreeFecProbeData(gpointer data)
{
    delete static_cast<FFecProbeData*>(data);
}

/**
 * Attach a probe on the src pad of the named element.
 * For post-FEC loss: attach to "fecdec" src pad (sees only packets that survived).
 * The callback receives (seq, userdata) for every packet that made it through FEC.
 * Returns true on success.
 */
extern "C" bool GStreamerAddFecProbe(
    void* pipeline,
    const char* element_name,
    void (*callback)(uint16_t seq, void* userdata),
    void* userdata)
{
    if (!pipeline || !callback) return false;

    GstElement* element = gst_bin_get_by_name(GST_BIN(pipeline), element_name);
    if (!element) {
        printf("[GStreamer] GStreamerAddFecProbe: element '%s' not found\n", element_name);
        return false;
    }

    GstPad* pad = gst_element_get_static_pad(element, "src");
    if (!pad) {
        printf("[GStreamer] GStreamerAddFecProbe: no src pad on '%s'\n", element_name);
        gst_object_unref(element);
        return false;
    }

    FFecProbeData* data = new FFecProbeData{ callback, userdata };

    gst_pad_add_probe(
        pad,
        GST_PAD_PROBE_TYPE_BUFFER,
        FecProbeCallback,
        data,
        FreeFecProbeData
    );

    gst_object_unref(pad);
    gst_object_unref(element);

    printf("[GStreamer] FEC probe attached to '%s' src pad\n", element_name);
    return true;
}

// ---------------------------------------------------------------------------
// Existing stats functions (unchanged)
// ---------------------------------------------------------------------------

extern "C" bool GStreamerGetJitterBufferStats(void* pipeline,
    unsigned long long* num_pushed,
    unsigned long long* num_lost,
    double* avg_jitter,
    unsigned long long* rtx_count)
{
    if (!pipeline) return false;

    GstElement* jitterbuffer = gst_bin_get_by_name(GST_BIN(pipeline), "jitterbuffer");
    if (!jitterbuffer) {
        GstIterator* it = gst_bin_iterate_elements(GST_BIN(pipeline));
        GValue item = G_VALUE_INIT;
        bool found = false;

        while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
            GstElement* element = GST_ELEMENT(g_value_get_object(&item));
            if (GST_IS_ELEMENT(element)) {
                GstElementFactory* factory = gst_element_get_factory(element);
                if (factory) {
                    const gchar* factory_name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
                    if (g_strcmp0(factory_name, "rtpjitterbuffer") == 0) {
                        jitterbuffer = GST_ELEMENT(gst_object_ref(element));
                        found = true;
                        g_value_reset(&item);
                        break;
                    }
                }
            }
            g_value_reset(&item);
        }
        g_value_unset(&item);
        gst_iterator_free(it);

        if (!found) return false;
    }

    GstStructure* stats = nullptr;
    g_object_get(jitterbuffer, "stats", &stats, nullptr);

    if (stats) {
        gst_structure_get_uint64(stats, "num-pushed", num_pushed);
        gst_structure_get_uint64(stats, "num-lost", num_lost);
        gst_structure_get_uint64(stats, "rtx-count", rtx_count);

        guint64 jitter_ns = 0;
        gst_structure_get_uint64(stats, "avg-jitter", &jitter_ns);
        *avg_jitter = jitter_ns / 1000000.0;

        gst_structure_free(stats);
        gst_object_unref(jitterbuffer);
        return true;
    }

    gst_object_unref(jitterbuffer);
    return false;
}

extern "C" bool GStreamerGetLatency(void* pipeline, double* latency_ms, double* jitter_buffer_latency_ms)
{
    if (!pipeline) return false;

    *latency_ms = 0.0;
    *jitter_buffer_latency_ms = 0.0;

    GstElement* jitterbuffer = gst_bin_get_by_name(GST_BIN(pipeline), "jitterbuffer");
    if (!jitterbuffer) {
        GstIterator* it = gst_bin_iterate_elements(GST_BIN(pipeline));
        GValue item = G_VALUE_INIT;

        while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
            GstElement* element = GST_ELEMENT(g_value_get_object(&item));
            if (GST_IS_ELEMENT(element)) {
                GstElementFactory* factory = gst_element_get_factory(element);
                if (factory) {
                    const gchar* factory_name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
                    if (g_strcmp0(factory_name, "rtpjitterbuffer") == 0) {
                        jitterbuffer = GST_ELEMENT(gst_object_ref(element));
                        g_value_reset(&item);
                        break;
                    }
                }
            }
            g_value_reset(&item);
        }
        g_value_unset(&item);
        gst_iterator_free(it);
    }

    if (jitterbuffer) {
        guint latency_prop = 0;
        g_object_get(jitterbuffer, "latency", &latency_prop, nullptr);
        *jitter_buffer_latency_ms = (double)latency_prop;
        gst_object_unref(jitterbuffer);
    }

    GstQuery* query = gst_query_new_latency();
    if (gst_element_query(GST_ELEMENT(pipeline), query)) {
        gboolean live;
        GstClockTime min_latency, max_latency;
        gst_query_parse_latency(query, &live, &min_latency, &max_latency);
        *latency_ms = min_latency / 1000000.0;
        gst_query_unref(query);
        return true;
    }

    gst_query_unref(query);
    return false;
}

extern "C" bool GStreamerGetSRTStats(void* pipeline,
    long long* packets_received,
    long long* packets_lost,
    double* rtt_ms)
{
    if (!pipeline) return false;

    GstElement* srtsrc = gst_bin_get_by_name(GST_BIN(pipeline), "srtsrc0");
    if (!srtsrc) {
        GstIterator* it = gst_bin_iterate_sources(GST_BIN(pipeline));
        GValue item = G_VALUE_INIT;

        while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
            GstElement* element = GST_ELEMENT(g_value_get_object(&item));
            GstElementFactory* factory = gst_element_get_factory(element);
            if (factory) {
                const gchar* factory_name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
                if (g_strcmp0(factory_name, "srtsrc") == 0) {
                    srtsrc = GST_ELEMENT(gst_object_ref(element));
                    g_value_reset(&item);
                    break;
                }
            }
            g_value_reset(&item);
        }
        g_value_unset(&item);
        gst_iterator_free(it);
    }

    if (!srtsrc) return false;

    GstStructure* stats = nullptr;
    g_object_get(srtsrc, "stats", &stats, nullptr);

    if (stats) {
        gst_structure_get_int64(stats, "packets-received", packets_received);
        gst_structure_get_int64(stats, "packets-lost", packets_lost);

        double rtt = 0;
        gst_structure_get_double(stats, "rtt-ms", &rtt);
        *rtt_ms = rtt;

        gst_structure_free(stats);
        gst_object_unref(srtsrc);
        return true;
    }

    gst_object_unref(srtsrc);
    return false;
}

extern "C" bool GStreamerPopBusError(void* bus, char* OutMsg, int OutMsgLen, bool* bOutIsWarning)
{
    if (!bus) return false;
    GstMessage* msg = gst_bus_pop_filtered(GST_BUS(bus),
        (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING));
    if (!msg) return false;

    GError* err = nullptr; gchar* dbg = nullptr;
    bool bWarn = (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_WARNING);
    if (bWarn)
        gst_message_parse_warning(msg, &err, &dbg);
    else
        gst_message_parse_error(msg, &err, &dbg);

    if (err && OutMsg && OutMsgLen > 0)
        snprintf(OutMsg, OutMsgLen, "%s  [debug: %s]", err->message, dbg ? dbg : "none");

    if (err) g_error_free(err);
    if (dbg) g_free(dbg);
    gst_message_unref(msg);

    if (bOutIsWarning) *bOutIsWarning = bWarn;
    return true;
}

extern "C" bool GStreamerNvdecAvailable()
{
    GstElementFactory* f = gst_element_factory_find("nvh264dec");
    if (f) { gst_object_unref(f); return true; }
    return false;
}