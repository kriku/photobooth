#include <dlfcn.h>
#include <glib-unix.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/gstinfo.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace std;

#define USE(x) ((void)(x))

static GstPipeline *gst_preview = nullptr;
static GstPipeline *gst_jpgenc = nullptr;
static string launch_string;
static GstElement *appsrc_;
static int frame_count = 0;

static fstream should_snap_file("should_snap",
                                ios::in | ios::out | ios::binary);
static char should_snap[1];
static char stop_snap[1] = {'0'};

static void appsink_eos(GstAppSink *appsink, gpointer user_data) {
  printf("app sink receive eos\n");
}

static GstFlowReturn new_buffer(GstAppSink *appsink, gpointer user_data) {
  GstSample *sample = NULL;

  frame_count++;

  g_signal_emit_by_name(appsink, "pull-sample", &sample, NULL);

  should_snap_file.seekg(0, ios::beg);
  should_snap_file.read(should_snap, 1);

  if (!should_snap_file) {
    printf("couldn't read should_snap file\n");
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }

  if (should_snap[0] != '1') {
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }

  should_snap_file.seekg(0, ios::beg);
  should_snap_file.write(stop_snap, 1);

  if (sample) {
    GstBuffer *buffer = NULL;
    GstCaps *caps = NULL;
    GstFlowReturn ret;

    caps = gst_sample_get_caps(sample);
    if (!caps) {
      printf("could not get snapshot format\n");
    }
    gst_caps_get_structure(caps, 0);
    buffer = gst_sample_get_buffer(sample);

    gst_buffer_ref(buffer);
    g_signal_emit_by_name(appsrc_, "push-buffer", buffer, &ret);

    gst_buffer_unref(buffer);
    gst_sample_unref(sample);
  } else {
    g_print("could not make snapshot\n");
  }

  return GST_FLOW_OK;
}

int main(int argc, char **argv) {
  USE(argc);
  USE(argv);

  time_t now = time(0);
  tm *gmtm = gmtime(&now);
  ostringstream current_date_str_stream;
  current_date_str_stream << put_time(gmtm, "%Y-%m-%d_%H-%M-%S");

  string current_date_str = current_date_str_stream.str();

  const char *current_date_final = current_date_str.c_str();

  mkdir(current_date_final, 0777);

  gst_init(&argc, &argv);

  GMainLoop *main_loop;
  main_loop = g_main_loop_new(NULL, FALSE);
  ostringstream launch_stream;

  // 4k
  // 3840 x 2160

  // max resolution
  // 4032 x 3040
  // 1432 x 1080 (fit fullhd display)

  int sensor_width = 3840;
  int sensor_height = 2160;
  int screen_width = 1080;
  int screen_height = 1920;
  int image_width = sensor_width;
  int image_height = sensor_height;

  GstAppSinkCallbacks callbacks = {appsink_eos, NULL, new_buffer};

  launch_stream
      // camera source
      << "nvarguscamerasrc ! "
      << "video/x-raw(memory:NVMM), "
      << "width=" << sensor_width << ", "
      << "height=" << sensor_height << ", "
      << "framerate=30/1 ! "
      // create tee duplicator
      << "tee name=t1 "
      << "t1. ! queue ! nvvidconv flip-method=7 ! "
      // output to hdmi0
      << "video/x-raw(memory:NVMM), "
      << "width=" << screen_width << ", "
      << "height=" << screen_height << ", "
      << "framerate=30/1 ! "
      << "nvoverlaysink "
      // pipe to jpegenc
      << "t1. ! queue ! nvvidconv flip-method=5 ! "
      << "video/x-raw, format=I420, "
      << "width=" << image_height << ", "
      << "height=" << image_width << " ! "
      << "appsink name=mysink ";

  launch_string = launch_stream.str();

  g_print("Preview string: %s\n", launch_string.c_str());

  GError *error = nullptr;
  gst_preview = (GstPipeline *)gst_parse_launch(launch_string.c_str(), &error);

  if (gst_preview == nullptr) {
    g_print("Failed to parse preview launch: %s\n", error->message);
    return -1;
  }

  if (error)
    g_error_free(error);

  GstElement *appsink_ = gst_bin_get_by_name(GST_BIN(gst_preview), "mysink");
  gst_app_sink_set_callbacks(GST_APP_SINK(appsink_), &callbacks, NULL, NULL);

  // jpegenc pipeline
  {
    launch_stream.str("");
    launch_stream.clear();

    launch_stream
        // snapshot source (from pipeline)
        << "appsrc name=mysource ! "
        << "video/x-raw, "
        << "width=" << image_height << ", "
        << "height=" << image_width << ", "
        << "format=I420, framerate=1/1 ! "
        << "nvjpegenc ! "
        << "multifilesink location=\"" << current_date_final
        << "/snap-%03d.jpg\" ";

    launch_string = launch_stream.str();

    g_print("JPEG encoding string: %s\n", launch_string.c_str());
    gst_jpgenc = (GstPipeline *)gst_parse_launch(launch_string.c_str(), &error);

    if (gst_jpgenc == nullptr) {
      g_print("Failed to parse jpeg launch: %s\n", error->message);
      return -1;
    }

    if (error)
      g_error_free(error);

    appsrc_ = gst_bin_get_by_name(GST_BIN(gst_jpgenc), "mysource");
    gst_app_src_set_stream_type(GST_APP_SRC(appsrc_),
                                GST_APP_STREAM_TYPE_STREAM);
  }

  gst_element_set_state((GstElement *)gst_jpgenc, GST_STATE_PLAYING);
  gst_element_set_state((GstElement *)gst_preview, GST_STATE_PLAYING);

  g_main_loop_run(main_loop);

  gst_element_set_state((GstElement *)gst_preview, GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(gst_preview));

  gst_app_src_end_of_stream((GstAppSrc *)appsrc_);
  // Wait for EOS message
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(gst_jpgenc));
  gst_bus_poll(bus, GST_MESSAGE_EOS, GST_CLOCK_TIME_NONE);

  gst_element_set_state((GstElement *)gst_jpgenc, GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(gst_jpgenc));

  g_main_loop_unref(main_loop);

  g_print("going to exit \n");
  return 0;
}
