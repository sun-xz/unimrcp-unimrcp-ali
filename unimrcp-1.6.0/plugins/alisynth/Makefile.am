AM_CPPFLAGS                = $(UNIMRCP_PLUGIN_INCLUDES) -std=c++11

plugin_LTLIBRARIES         = alisynth.la

alisynth_la_SOURCES       = src/Ali_SynthEngine.cpp src/Ali_Synth_engine.cpp src/Ali_Synth_pool.cpp src/Ali_Synth_util.cpp src/Ali_SynthChannel.cpp src/Ali_SynthChannelFactory.cpp
alisynth_la_LDFLAGS       = $(UNIMRCP_PLUGIN_OPTS) \
                              -L$(top_srcdir)/libs/NlsSdkCpp3.X/lib/ \
                              -L/usr/local/apr/lib/ \
                              -lalibabacloud-idst-common -lalibabacloud-idst-speech -lpthread asrclient unimrcpclient unimrcpserver apr-1 aprutil-1 -lexpat \
							  -ldl -lrt -lstdc++

include $(top_srcdir)/build/rules/uniplugin.am

UNIMRCP_PLUGIN_INCLUDES     += -Iinclude
UNIMRCP_PLUGIN_INCLUDES     += -I/usr/local/apr/include/apr-1
UNIMRCP_PLUGIN_INCLUDES     += -I$(top_srcdir)/libs/NlsSdkCpp3.X/include/
