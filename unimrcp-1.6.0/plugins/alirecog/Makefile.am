AM_CPPFLAGS                = $(UNIMRCP_PLUGIN_INCLUDES) -std=c++11

plugin_LTLIBRARIES         = alirecog.la

alirecog_la_SOURCES       = src/AliEngine.cpp src/Ali_recog_pool.cpp src/Ali_recog_engine.cpp src/Ali_channel_factory.cpp src/Ali_channel.cpp
alirecog_la_LDFLAGS       = $(UNIMRCP_PLUGIN_OPTS) \
                              -L$(top_srcdir)/libs/NlsSdkCpp3.X/lib/ \
                              -L/usr/local/apr/lib/ \
                              -lalibabacloud-idst-common -lalibabacloud-idst-speech -lpthread asrclient unimrcpclient unimrcpserver apr-1 aprutil-1 -lexpat \
							  -ldl -lrt -lstdc++

include $(top_srcdir)/build/rules/uniplugin.am

UNIMRCP_PLUGIN_INCLUDES     += -Iinclude
UNIMRCP_PLUGIN_INCLUDES     += -I/usr/local/apr/include/apr-1
UNIMRCP_PLUGIN_INCLUDES     += -I$(top_srcdir)/libs/NlsSdkCpp3.X/include/
