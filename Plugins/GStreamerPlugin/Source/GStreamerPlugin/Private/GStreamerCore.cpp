#ifdef GError
#undef GError
#endif

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <stdio.h>
#include <string.h>

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

// Get appsink from pipeline by name
extern "C" void* GStreamerGetElementByName(void* pipeline, const char* name)
{
    if (!pipeline) return nullptr;
    return gst_bin_get_by_name(GST_BIN(pipeline), name);
}

// Pull a sample from appsink (BLOCKING)
extern "C" void* GStreamerPullSample(void* appsink)
{
    if (!appsink) return nullptr;
    return gst_app_sink_pull_sample(GST_APP_SINK(appsink));
}

// NEW: Try to pull a sample with timeout (NON-BLOCKING)
// This is critical for preventing game thread stalls with hardware decoding
extern "C" void* GStreamerTryPullSample(void* appsink, double timeout_seconds)
{
    if (!appsink) return nullptr;
    
    GstClockTime timeout_ns = (GstClockTime)(timeout_seconds * GST_SECOND);
    return gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), timeout_ns);
}

// NEW: Check if appsink has samples available without blocking
extern "C" bool GStreamerIsEOS(void* appsink)
{
    if (!appsink) return true;
    return gst_app_sink_is_eos(GST_APP_SINK(appsink));
}

// NEW: Get buffer from sample
extern "C" void* GStreamerGetSampleBuffer(void* sample)
{
    if (!sample) return nullptr;
    return gst_sample_get_buffer(GST_SAMPLE(sample));
}

// Get caps from sample
extern "C" void* GStreamerGetSampleCaps(void* sample)
{
    if (!sample) return nullptr;
    return gst_sample_get_caps(GST_SAMPLE(sample));
}

// Get video dimensions from caps
extern "C" bool GStreamerGetVideoDimensions(void* caps, int* width, int* height)
{
    if (!caps) return false;

    GstStructure* structure = gst_caps_get_structure(GST_CAPS(caps), 0);
    if (!structure) return false;

    return gst_structure_get_int(structure, "width", width) &&
        gst_structure_get_int(structure, "height", height);
}

// Map buffer and copy data
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

// Get buffer size
extern "C" int GStreamerGetBufferSize(void* buffer)
{
    if (!buffer) return 0;
    return (int)gst_buffer_get_size(GST_BUFFER(buffer));
}

// Free sample
extern "C" void GStreamerFreeSample(void* sample)
{
    if (sample) {
        gst_sample_unref(GST_SAMPLE(sample));
    }
}

// Unref element
extern "C" void GStreamerUnrefElement(void* element)
{
    if (element) {
        gst_object_unref(GST_OBJECT(element));
    }
}

// Get bus from pipeline
extern "C" void* GStreamerGetBus(void* pipeline)
{
    if (!pipeline) return nullptr;
    return gst_element_get_bus(GST_ELEMENT(pipeline));
}

// Check for bus messages (non-blocking)
extern "C" void* GStreamerPollBusMessage(void* bus, double timeout_seconds)
{
    if (!bus) return nullptr;
    return gst_bus_timed_pop(GST_BUS(bus), (GstClockTime)(timeout_seconds * GST_SECOND));
}

// Get message type
extern "C" int GStreamerGetMessageType(void* message)
{
    if (!message) return 0;
    return GST_MESSAGE_TYPE(GST_MESSAGE(message));
}

// Free message
extern "C" void GStreamerFreeMessage(void* message)
{
    if (message) {
        gst_message_unref(GST_MESSAGE(message));
    }
}

// Unref bus
extern "C" void GStreamerUnrefBus(void* bus)
{
    if (bus) {
        gst_object_unref(GST_OBJECT(bus));
    }
}

// Get statistics from rtpjitterbuffer
extern "C" bool GStreamerGetJitterBufferStats(void* pipeline,
    unsigned long long* num_pushed,
    unsigned long long* num_lost,
    double* avg_jitter,
    unsigned long long* rtx_count)
{
    if (!pipeline) return false;

    // Get the jitterbuffer element by name first
    GstElement* jitterbuffer = gst_bin_get_by_name(GST_BIN(pipeline), "jitterbuffer");
    if (!jitterbuffer) {
        // Try to find it by type
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

    // Get statistics structure
    GstStructure* stats = nullptr;
    g_object_get(jitterbuffer, "stats", &stats, nullptr);

    if (stats) {
        gst_structure_get_uint64(stats, "num-pushed", num_pushed);
        gst_structure_get_uint64(stats, "num-lost", num_lost);
        gst_structure_get_uint64(stats, "rtx-count", rtx_count);

        // Jitter is in nanoseconds, convert to milliseconds
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

// Get decoder statistics
extern "C" bool GStreamerGetDecoderStats(void* pipeline,
    unsigned long long* frames_decoded,
    unsigned long long* frames_dropped)
{
    if (!pipeline) return false;

    // Find decoder element (avdec_h264 or nvh264dec)
    GstElement* decoder = nullptr;
    GstIterator* it = gst_bin_iterate_elements(GST_BIN(pipeline));
    GValue item = G_VALUE_INIT;

    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
        GstElement* element = GST_ELEMENT(g_value_get_object(&item));
        if (GST_IS_ELEMENT(element)) {
            GstElementFactory* factory = gst_element_get_factory(element);
            if (factory) {
                const gchar* factory_name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
                if (g_str_has_prefix(factory_name, "avdec_") || 
                    g_str_has_prefix(factory_name, "nvh264") ||
                    g_str_has_prefix(factory_name, "nvdec")) {
                    decoder = GST_ELEMENT(gst_object_ref(element));
                    g_value_reset(&item);
                    break;
                }
            }
        }
        g_value_reset(&item);
    }
    g_value_unset(&item);
    gst_iterator_free(it);

    if (!decoder) return false;

    // Query statistics (these are rough estimates)
    GstQuery* query = gst_query_new_latency();
    if (gst_element_query(decoder, query)) {
        // Decoder-specific stats would go here
        // For now, we'll get frame info from QoS events
    }
    gst_query_unref(query);

    gst_object_unref(decoder);
    return false;  // Not all decoders expose frame stats easily
}

// Get current latency
extern "C" bool GStreamerGetLatency(void* pipeline, double* latency_ms, double* jitter_buffer_latency_ms)
{
    if (!pipeline) return false;

    *latency_ms = 0.0;
    *jitter_buffer_latency_ms = 0.0;

    // Get configured jitter buffer latency
    GstElement* jitterbuffer = gst_bin_get_by_name(GST_BIN(pipeline), "jitterbuffer");
    if (!jitterbuffer) {
        // Try to find it by factory name
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
        *jitter_buffer_latency_ms = (double)latency_prop;  // Already in milliseconds
        gst_object_unref(jitterbuffer);
    }

    // Query pipeline latency
    GstQuery* query = gst_query_new_latency();
    if (gst_element_query(GST_ELEMENT(pipeline), query)) {
        gboolean live;
        GstClockTime min_latency, max_latency;
        gst_query_parse_latency(query, &live, &min_latency, &max_latency);

        *latency_ms = min_latency / 1000000.0;  // Convert ns to ms

        gst_query_unref(query);
        return true;
    }

    gst_query_unref(query);
    return false;
}


// Get SRT statistics
extern "C" bool GStreamerGetSRTStats(void* pipeline,
    long long* packets_received,
    long long* packets_lost,
    double* rtt_ms)
{
    if (!pipeline) return false;

    GstElement* srtsrc = gst_bin_get_by_name(GST_BIN(pipeline), "srtsrc0");
    if (!srtsrc) {
        // Try to find by factory
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