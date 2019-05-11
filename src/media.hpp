/*
 *
 *  Attract-Mode frontend
 *  Copyright (C) 2013 Andrew Mickelson
 *
 *  This file is part of Attract-Mode.
 *
 *  Attract-Mode is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Attract-Mode is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Attract-Mode.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef MEDIA_HPP
#define MEDIA_HPP

#include <Audio/SoundStream.hpp>
#include <SFML/System.hpp>
#include <SFML/Graphics.hpp>
#include <vector>
#include <string>
#include <queue>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#define FE_HWACCEL (LIBAVUTIL_VERSION_INT >= AV_VERSION_INT( 55, 78, 100 ))

#if FE_HWACCEL
#include <libavutil/hwcontext.h>
#endif

#if USE_SWRESAMPLE
 #include <libswresample/swresample.h>
 #include <libavutil/opt.h>
 #define DO_RESAMPLE
 #define RESAMPLE_LIB_STR " / swresample "
 #define RESAMPLE_VERSION_MAJOR LIBSWRESAMPLE_VERSION_MAJOR
 #define RESAMPLE_VERSION_MINOR LIBSWRESAMPLE_VERSION_MINOR
 #define RESAMPLE_VERSION_MICRO LIBSWRESAMPLE_VERSION_MICRO
 typedef SwrContext ResampleContext;
 inline void resample_free( ResampleContext **ctx ) { swr_free( ctx ); }
 inline ResampleContext *resample_alloc() { return swr_alloc(); }
 inline int resample_init( ResampleContext *ctx ) { return swr_init( ctx ); }
#else
 #if USE_AVRESAMPLE
  #include <libavresample/avresample.h>
  #include <libavutil/opt.h>
  #define DO_RESAMPLE
  #define RESAMPLE_LIB_STR " / avresample "
  #define RESAMPLE_VERSION_MAJOR LIBAVRESAMPLE_VERSION_MAJOR
  #define RESAMPLE_VERSION_MINOR LIBAVRESAMPLE_VERSION_MINOR
  #define RESAMPLE_VERSION_MICRO LIBAVRESAMPLE_VERSION_MICRO
  typedef AVAudioResampleContext ResampleContext;
  inline void resample_free( ResampleContext **ctx ) { avresample_free( ctx ); }
  inline ResampleContext *resample_alloc() { return avresample_alloc_context(); }
  inline int resample_init( ResampleContext *ctx ) { return avresample_open( ctx ); }
 #endif
#endif

}

class FeMediaImp;
class FeAudioImp;
class FeVideoImp;
struct AVCodec;
struct AVCodecContext;

namespace sf
{
	class Texture;
};

class FeMedia : private sf::SoundStream
{
friend class FeVideoImp;

public:
	enum Type
	{
		Audio=0x01,
		Video=0x02,
		AudioVideo=0x03
	};

	FeMedia( Type t );
	~FeMedia();

	bool open( const std::string &archive,
			const std::string &name,
			sf::Texture *out_texture=NULL );

	using sf::SoundStream::setPosition;
	using sf::SoundStream::getPosition;
	using sf::SoundStream::setPitch;
	using sf::SoundStream::getPitch;
	using sf::SoundStream::getStatus;
	using sf::SoundStream::setLoop;
	using sf::SoundStream::getLoop;
	using sf::SoundSource::release_audio;

	void play();
	void stop();
	void close();

	// tick() needs to be called regularly on video media to update the display
	// texture. Returns true if display refresh required.  false if no update
	//
	bool tick();

	void setVolume(float volume);

	bool is_playing();
	bool is_multiframe() const;
	float get_aspect_ratio() const;

	sf::Time get_video_time();
	sf::Time get_duration() const;

	const char *get_metadata( const char *tag );

	//
	// return true if the given filename is a media file that can be opened
	//	by FeMedia
	//
	static bool is_supported_media_file( const std::string &filename );

	//
	static void get_decoder_list( std::vector < std::string > &l );

	// get/set video decoder to be used (if available)
	//
	static std::string get_current_decoder();
	static void set_current_decoder( const std::string & );

protected:
	static std::string g_decoder;

	// overrides from base class
	//
	bool onGetData( Chunk &data );
	void onSeek( sf::Time timeOffset );

	bool read_packet();
	bool end_of_file();

	void try_hw_accel( AVCodecContext *& ctx, AVCodec *&dec );

private:
	FeMediaImp *m_imp;
	FeAudioImp *m_audio;
	FeVideoImp *m_video;

	FeMedia( const FeMedia & );
	FeMedia &operator=( const FeMedia & );
	static void init_av();
	float m_aspect_ratio;
};

//
// Container for our general implementation
//
class FeMediaImp
{
public:
	FeMediaImp( FeMedia::Type t );
	void close();

	FeMedia::Type m_type;
	AVFormatContext *m_format_ctx;
	AVIOContext *m_io_ctx;
	sf::Mutex m_read_mutex;
	bool m_read_eof;
};

//
// Base class for our implementation of the audio and video components
//
class FeBaseStream
{
private:
	//
	// Queue containing the next packet to process for this stream
	//
	std::queue <AVPacket *> m_packetq;
	sf::Mutex m_packetq_mutex;

public:
	virtual ~FeBaseStream();

	bool at_end;					// set when at the end of our input
	bool far_behind;
	AVCodecContext *codec_ctx;
	AVCodec *codec;
	int stream_id;

	FeBaseStream();
	virtual void stop();
	AVPacket *pop_packet();
	void push_packet( AVPacket *pkt );
	void clear_packet_queue();

	// Utility functions to free AV stuff...
	//
	static void free_packet( AVPacket *pkt );
	static void free_frame( AVFrame *frame );
};

//
// Container for our implementation of the audio component
//
class FeAudioImp : public FeBaseStream
{
public:
#ifdef DO_RESAMPLE
	ResampleContext *resample_ctx;
#endif
	sf::Int16 *buffer;
	sf::Mutex buffer_mutex;

	FeAudioImp();
	~FeAudioImp();
};

//
// Container for our implementation of the video component
//
class FeVideoImp : public FeBaseStream
{
private:
	//
	// Video decoding and colour conversion runs on a dedicated thread.
	// Loading the result into an sf::Texture and displaying it is done
	// on the main thread.
	//
	sf::Thread m_video_thread;
	FeMedia *m_parent;
	sf::Uint8 *rgba_buffer[4];
	int rgba_linesize[4];

#if FE_HWACCEL
	AVPixelFormat hwaccel_output_format;
	bool hw_retrieve_data( AVFrame *f );
#endif

public:
	bool run_video_thread;
	sf::Time time_base;
	sf::Time max_sleep;
	sf::Clock video_timer;
	sf::Texture *display_texture;
	SwsContext *sws_ctx;
	int sws_flags;
	int disptex_width;
	int disptex_height;

	//
	// The video thread sets display_frame when the next image frame is decoded.
	// The main thread then copies the image into the corresponding sf::Texture.
	//
	sf::Mutex image_swap_mutex;
	sf::Uint8 *display_frame;

	FeVideoImp( FeMedia *parent );
	~FeVideoImp();

	void play();
	void stop();

	void preload();
	void video_thread();
};

#endif
