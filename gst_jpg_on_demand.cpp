#include <cstdlib>
#include <gst/gst.h>
#include <gst/gstinfo.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <glib-unix.h>
#include <dlfcn.h>

#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

using namespace std;

#define USE(x) ((void)(x))

static GstPipeline *gst_preview = nullptr;
static GstPipeline *gst_jpgenc = nullptr;
static string launch_string;   
static GstElement *appsrc_;
static int frame_count = 0;

static void appsink_eos(GstAppSink * appsink, gpointer user_data)
{
    printf("app sink receive eos\n");
//    g_main_loop_quit (hpipe->loop);
}

static GstFlowReturn new_buffer(GstAppSink *appsink, gpointer user_data)
{
    GstSample *sample = NULL;

    frame_count ++;
    printf("frame count %d\n", frame_count);

    if (frame_count % 50 != 0) {
        g_signal_emit_by_name (appsink, "pull-sample", &sample, NULL);
        return GST_FLOW_OK;
    }

    printf("lets make a snapshot\n");

    g_signal_emit_by_name (appsink, "pull-sample", &sample, NULL);

    if (sample)
    {
        GstBuffer *buffer = NULL;
        GstCaps   *caps   = NULL;
        GstFlowReturn ret;

        caps = gst_sample_get_caps (sample);
        if (!caps)
        {
            printf("could not get snapshot format\n");
        }
        gst_caps_get_structure (caps, 0);
        buffer = gst_sample_get_buffer (sample);

        gst_buffer_ref(buffer);
        g_signal_emit_by_name (appsrc_, "push-buffer", buffer, &ret);

        gst_buffer_unref(buffer);
        gst_sample_unref (sample);
    }
    else
    {
        g_print ("could not make snapshot\n");
    }

    return GST_FLOW_OK;
}

int main(int argc, char** argv) {
    USE(argc);
    USE(argv);

    gst_init (&argc, &argv);

    GMainLoop *main_loop;
    main_loop = g_main_loop_new (NULL, FALSE);
    ostringstream launch_stream;
    // 3840 x 2160
    // 4032 3040
    // 1432 1080

    int sensor_width = 3840;
    int sensor_height = 2160;
    int screen_width = 1080;
    int screen_height = 1920;
    int image_width = sensor_width;
    int image_height = sensor_height;

    GstAppSinkCallbacks callbacks = {appsink_eos, NULL, new_buffer};

    // << "t1. ! queue ! nvvidconv ! "
    launch_stream
    << "nvarguscamerasrc ! "
    << "video/x-raw(memory:NVMM), "
    << "width="<< sensor_width <<", height="<< sensor_height <<", framerate=30/1 ! "
    << "tee name=t1 "
    << "t1. ! queue ! nvvidconv flip-method=7 ! "
    << "video/x-raw(memory:NVMM), "
    << "width="<< screen_width <<", height="<< screen_height <<", framerate=30/1 ! "
    << " nvoverlaysink "
    << "t1. ! queue ! nvvidconv ! "
    << "video/x-raw, format=I420, width="<< image_width <<", height="<< image_height <<" ! "
    << "appsink name=mysink ";

    launch_string = launch_stream.str();

    g_print("Preview string: %s\n", launch_string.c_str());

    GError *error = nullptr;
    gst_preview  = (GstPipeline*) gst_parse_launch(launch_string.c_str(), &error);

    if (gst_preview == nullptr) {
        g_print( "Failed to parse preview launch: %s\n", error->message);
        return -1;
    }

    if (error) g_error_free(error);

    GstElement *appsink_ = gst_bin_get_by_name(GST_BIN(gst_preview), "mysink");
    gst_app_sink_set_callbacks (GST_APP_SINK(appsink_), &callbacks, NULL, NULL);

    // jpegenc pipeline
    {
        launch_stream.str("");
        launch_stream.clear();
        launch_stream
        << "appsrc name=mysource ! "
        << "video/x-raw,width="<< image_width <<",height="<< image_height <<",format=I420,framerate=1/1 ! "
        << "nvjpegenc ! "
        << "multifilesink location=snap-%03d.jpg ";

        launch_string = launch_stream.str();

        g_print("JPEG encoding string: %s\n", launch_string.c_str());
        gst_jpgenc = (GstPipeline*) gst_parse_launch(launch_string.c_str(), &error);

        if (gst_jpgenc == nullptr) {
            g_print( "Failed to parse jpeg launch: %s\n", error->message);
            return -1;
        }
        if(error) g_error_free(error);
        appsrc_ = gst_bin_get_by_name(GST_BIN(gst_jpgenc), "mysource");
        gst_app_src_set_stream_type(GST_APP_SRC(appsrc_), GST_APP_STREAM_TYPE_STREAM);
    }

    gst_element_set_state((GstElement*)gst_jpgenc, GST_STATE_PLAYING);
    gst_element_set_state((GstElement*)gst_preview, GST_STATE_PLAYING);

    sleep(5);
    // g_main_loop_run(main_loop);

    gst_element_set_state((GstElement*)gst_preview, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(gst_preview));

    gst_app_src_end_of_stream((GstAppSrc *)appsrc_);
    // Wait for EOS message
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(gst_jpgenc));
    gst_bus_poll(bus, GST_MESSAGE_EOS, GST_CLOCK_TIME_NONE);
    gst_element_set_state((GstElement*)gst_jpgenc, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(gst_jpgenc));

    g_main_loop_unref(main_loop);

    g_print("going to exit \n");
    return 0;
}
