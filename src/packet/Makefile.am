AM_CPPFLAGS = -I$(srcdir)/../util $(CXX11_FLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

noinst_LIBRARIES = libpacket.a

libpacket_a_SOURCES = packetshell.hh packetshell.cc queued_packet.hh \
                      abstract_packet_queue.hh dropping_packet_queue.hh dropping_packet_queue.cc infinite_packet_queue.hh \
                      drop_tail_packet_queue.hh drop_head_packet_queue.hh \
                      codel_packet_queue.cc codel_packet_queue.hh \
                      pie_packet_queue.cc pie_packet_queue.hh \
                      bindworkaround.hh \
					  packet_header.hh \
					  drop_bitrate_dequeue_queue.hh drop_bitrate_dequeue_queue.cc \
					  drop_activenet_queue.hh  drop_activenet_queue.cc
