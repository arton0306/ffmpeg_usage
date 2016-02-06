#include <stdio.h>
#include <assert.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "libavutil/avutil.h"

#define true 1
#define false 0

#define ASSERT_OR_ERRMSG(cond, err_msg) \
    if (! (cond)) { \
        printf(err_msg); \
        assert(false); \
    }

int get_stream_idx( AVFormatContext * fmt_ctx, enum AVMediaType media_type )
{
    for ( int i = 0; i < fmt_ctx->nb_streams; ++i ) {
        if ( fmt_ctx->streams[i]->codec->codec_type == media_type ) {
            return i;
        }
    }
    return -1;
}

AVCodecContext * new_codec_ctx( AVFormatContext * fmt_ctx, int stream_idx )
{
    AVCodecContext * codec_ctx = fmt_ctx->streams[stream_idx]->codec;
    AVCodec * codec = avcodec_find_decoder( codec_ctx->codec_id );
    assert(codec);

    int err;
    AVCodecContext *copy_codec_ctx = avcodec_alloc_context3(codec);
    err = avcodec_copy_context(copy_codec_ctx, codec_ctx);
    assert(err == 0);

    err = avcodec_open2( copy_codec_ctx, codec, NULL );
    assert(err >= 0);

    return copy_codec_ctx;
}

void interleave(uint8_t **data, uint8_t *outbuf, int channels,
    enum AVSampleFormat sample_fmt, int data_bytes)
{
    assert(av_sample_fmt_is_planar(sample_fmt));
    int sample_bytes = av_get_bytes_per_sample(sample_fmt);
    assert(data_bytes % (channels*sample_bytes) == 0);
    int chn_bytes = data_bytes / channels;
    for (int i = 0; i < chn_bytes; i+=sample_bytes) {
        for (int chn = 0; chn < channels; chn++) {
            memcpy(outbuf, data[chn]+i, sample_bytes);
            outbuf += sample_bytes;
        }
    }
}

void write_pcm( void const * buffer, int size, char const * file_name )
{
    FILE * outfile = fopen( file_name, "ab" );
    assert( outfile );
    fwrite( buffer, 1, size, outfile );
    fclose( outfile );
}

char const * video_filename;
char const * pcm_filaname;

int main(int argc, char *argv[]) {
    if (argc == 3) {
        video_filename = argv[1];
        pcm_filaname = argv[2];
    } else {
        printf("Missing input video file or output file.\n");
        return 1;
    }

    int err;
    av_register_all();
    AVFormatContext * fmt_ctx = NULL;
    err = avformat_open_input( &fmt_ctx, video_filename, NULL, NULL );
    assert( err == 0 );
    err = avformat_find_stream_info( fmt_ctx, NULL );
    assert( err >= 0 );
    av_dump_format( fmt_ctx, 0, argv[1], 0 );

    // int const video_idx = get_stream_idx( fmt_ctx, AVMEDIA_TYPE_VIDEO );
    int const audio_idx = get_stream_idx( fmt_ctx, AVMEDIA_TYPE_AUDIO );
    assert(audio_idx >= 0);

    // AVCodecContext * video_codec_ctx = new_codec_ctx( fmt_ctx, video_idx );
    AVCodecContext * audio_codec_ctx = new_codec_ctx( fmt_ctx, audio_idx );

    int is_frame_finish;

    static AVPacket pkt;
    static uint8_t *pkt_data = NULL;
    static int pkt_size = 0;
    static AVFrame frame;
    AVFrame *decoded_frame = av_frame_alloc();
    assert( decoded_frame );

    while ( true ) {
        if ( av_read_frame( fmt_ctx, &pkt ) < 0 ) {
            break;
        }

        if ( pkt.stream_index == audio_idx )
        {
            pkt_data = pkt.data;
            pkt_size = pkt.size;
            while ( pkt_size > 0 )
            {
                int const byte_consumed = avcodec_decode_audio4(
                    audio_codec_ctx, decoded_frame, &is_frame_finish, &pkt );
                ASSERT_OR_ERRMSG(byte_consumed >= 0, "Errors when decode audio.\n");

                pkt_data += byte_consumed;
                pkt_size -= byte_consumed;
                assert(pkt_size >= 0);
                if ( is_frame_finish ) {
                    int const data_size = av_samples_get_buffer_size(
                        NULL, audio_codec_ctx->channels,
                        decoded_frame->nb_samples,
                        audio_codec_ctx->sample_fmt,
                        1);

                    if ( av_sample_fmt_is_planar(audio_codec_ctx->sample_fmt)) {
                        uint8_t *buf = malloc(data_size);
                        interleave(decoded_frame->data, buf,
                            audio_codec_ctx->channels, audio_codec_ctx->sample_fmt, data_size);
                        write_pcm( buf, data_size, pcm_filaname );
                        free(buf);
                    } else {
                        write_pcm( decoded_frame->data[0], data_size, pcm_filaname );
                    }
                }
            }
            if (pkt.data) {
                av_free_packet( &pkt );
            }
        }
        else
        {
            av_free_packet( &pkt );
        }
    }

    avcodec_close(audio_codec_ctx);
    avformat_close_input(&fmt_ctx);

    printf("audio decode done\n");
    return 0;
}
