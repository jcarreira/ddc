include $(top_srcdir)/common.mk

AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS =  pr

LIBS         =  -lclient \
                -lauthentication -lutils -lcommon -levictionpolicies \
                $(LIBRDMACM) $(LIBIBVERBS)

LINCLUDES    =  -L$(top_srcdir)/src/utils/ \
                -L$(top_srcdir)/src/client/ \
                -L$(top_srcdir)/src/authentication \
                -L$(top_srcdir)/src/common \
                -L$(top_srcdir)/src/cache_manager

CPPFLAGS =  -ggdb -O3 -I$(top_srcdir) -I$(top_srcdir)/examples/sparsehash/src/ \
		-isystem $(top_srcdir)/third_party/libcuckoo/ \
		-I$(top_srcdir)/src \
		-I$(top_srcdir)/third_party/flatbuffers/include
SOURCES  =  PageRank.cpp Input.cpp Vertex.cpp

pr_SOURCES  =  pr.cpp $(SOURCES)
pr_LDFLAGS  =  -pthread
pr_LDADD    =  $(LINCLUDES) $(LIBS)

