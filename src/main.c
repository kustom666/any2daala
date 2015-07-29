#include <daala/codec.h>
#include <daala/daalaenc.h>
#include <daala/daala_integer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ogg/ogg.h>

#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>

int main(int argc, char** argv)
{
	int quant = 30;
	int retval, got_frame, nb_frames, curr_frame;
	long video_sn = 1;

	clock_t t;

	FILE* db_out;
	daala_info info_codec;
	daala_enc_ctx* context;
	daala_comment comment;
	ogg_packet header_packet;
	ogg_packet data_packet;
	od_img image;
	
	AVPacket pkt;
	AVFormatContext* fmt_ctx = NULL;
	AVStream* video_stream = NULL;
	AVFrame* frame;
	AVCodecContext* video_dec_ctx;

	db_out = fopen(argv[2], "wb");
	
	av_register_all();
	av_log_set_level(AV_LOG_QUIET);

	int ret = avformat_open_input(&fmt_ctx, argv[1], NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open source file %s\n", argv[1]);
        exit(1);
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    av_dump_format(fmt_ctx, 0, argv[1], 0);

    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    retval = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find a video stream in input file '%s'\n", argv[1]);
        exit(1);
    }

    video_stream = fmt_ctx->streams[retval];
    nb_frames = video_stream->nb_frames;
    /* find decoder for the stream */
    video_dec_ctx = video_stream->codec;
    dec = avcodec_find_decoder(video_dec_ctx->codec_id);
    if (!dec) {
        fprintf(stderr, "Failed to find a video codec\n");
        exit(1);
    }

    if ((ret = avcodec_open2(video_dec_ctx, dec, &opts)) < 0) {
        fprintf(stderr, "Failed to open a video codec\n");
        exit(1);
    }

	frame = av_frame_alloc();

	daala_info_init(&info_codec);
	daala_comment_init(&comment);

	daala_log_init();
	const char* dvstring = daala_version_string();
	fprintf(stderr, "Using %s\n", dvstring);

	info_codec.pic_width = video_dec_ctx->width;
	info_codec.pic_height = video_dec_ctx->height;

	info_codec.pixel_aspect_numerator = 1;
	info_codec.pixel_aspect_denominator = 1;

	info_codec.timebase_numerator = video_dec_ctx->time_base.den;
	info_codec.timebase_denominator = video_dec_ctx->time_base.num;

	info_codec.frame_duration = 1;

	int xdeccb = 0, ydeccb = 0, xdeccr = 0, ydeccr = 0;

	if(video_dec_ctx->pix_fmt == AV_PIX_FMT_YUV420P)
	{
		xdeccb = 1;
		ydeccb = 1;
		xdeccr = 1;
		ydeccr = 1;
	}
	else if(video_dec_ctx->pix_fmt == AV_PIX_FMT_YUV422P)
	{
		xdeccb = 1;
		ydeccb = 0;
		xdeccr = 1;
		ydeccr = 0;
	}
	else if(video_dec_ctx->pix_fmt == AV_PIX_FMT_YUV444P)
	{
		xdeccb = 1;
		ydeccb = 0;
		xdeccr = 1;
		ydeccr = 0;
	}

	info_codec.plane_info[0].xdec = 0;
	info_codec.plane_info[0].ydec = 0;

	info_codec.plane_info[1].xdec = xdeccb;
	info_codec.plane_info[1].ydec = ydeccb;

	info_codec.plane_info[2].xdec = xdeccr;
	info_codec.plane_info[2].ydec = ydeccr;

	info_codec.keyframe_rate = 10;
	info_codec.nplanes = 3;

	image.nplanes = 3;
	image.width = video_dec_ctx->width;
	image.height = video_dec_ctx->height;

	context = daala_encode_create(&info_codec);
	daala_encode_ctl(context, OD_SET_QUANT, &quant, sizeof(quant));

	ogg_stream_state os;
	ogg_page og;

	if((retval = ogg_stream_init(&os, video_sn)) != 0)
	{
		fprintf(stderr, "Error when initializing the output ogg stream\n");
	}

	while((retval = daala_encode_flush_header(context, &comment, &header_packet)) != 0)
	{
		ogg_stream_packetin(&os, &header_packet);
		while((retval = ogg_stream_pageout(&os, &og)) != 0)
		{
			fwrite(og.header, 1, og.header_len, db_out);
			fwrite(og.body, 1, og.body_len, db_out);
		}
	}

	int i=1;
	float fps =0;
	while(av_read_frame(fmt_ctx, &pkt) == 0)
	{ //TODO : check return for errors
		if(avcodec_decode_video2(video_dec_ctx, frame, &got_frame, &pkt) < 0)
		{
			fprintf(stderr, "Error when decoding the input video\n");
		}
		if(got_frame != 0)
		{
			int y =0;
			unsigned char* buffer;
			
			t = clock();	
			y = avpicture_get_size(video_dec_ctx->pix_fmt, frame->width, frame->height);
			buffer = malloc(y*sizeof(unsigned char));
			avpicture_layout((AVPicture*)frame, video_dec_ctx->pix_fmt, frame->width, frame->height, buffer, y);

			image.planes[0].data = buffer;
			image.planes[0].xdec = 0;
			image.planes[0].ydec = 0;
			image.planes[0].xstride = 1;
			image.planes[0].ystride = frame->linesize[0];

			image.planes[1].data = buffer+frame->linesize[0]*frame->height;
			image.planes[1].xdec = xdeccb;
			image.planes[1].ydec = ydeccb;
			image.planes[1].xstride = 1;
			image.planes[1].ystride = frame->linesize[1];

			image.planes[2].data = buffer+frame->linesize[0]*frame->height+frame->linesize[1]*frame->height/2;
			image.planes[2].xdec = xdeccr;
			image.planes[2].ydec = ydeccr;
			image.planes[2].xstride = 1;
			image.planes[2].ystride = frame->linesize[2];

			switch(retval = daala_encode_img_in (context, &image, 0))
			{
				case 0:
				break;
				case OD_EFAULT:
					fprintf(stderr, "Error when encoding the image : no image data\n");
					break;
				case OD_EINVAL:
					fprintf(stderr, "Error when encoding the image : image size different from video size\n");
					break;
				default:
					break;
			}

			while((retval=daala_encode_packet_out (context, 0, &data_packet)) != 0)
			{
				data_packet.packetno = i;
				if(ogg_stream_packetin(&os, &data_packet) != 0)
				{
					fprintf(stderr, "Could not write the output video : Internal libogg error\n");
				}
				while((retval = ogg_stream_pageout(&os, &og)) != 0)
				{
					fwrite(og.header, 1, og.header_len, db_out);
					fwrite(og.body, 1, og.body_len, db_out);
				}
			}
			
			t = clock() - t;
			fps=1/((float)t/CLOCKS_PER_SEC);
			int remaining_seconds = nb_frames-i/fps;
			fprintf(stderr, "%79s\r", " ");
			fprintf(stderr, "Encoding frame %d of %d\t %f FPS\t %02d:%02d remaining \r", i, nb_frames, fps, remaining_seconds/60, remaining_seconds%60);
			++i;
		}
	}

	while((retval = ogg_stream_flush(&os, &og)) != 0)
	{
		fwrite(og.header, 1, og.header_len, db_out);
		fwrite(og.body, 1, og.body_len, db_out);
	}

	fprintf(stderr, "\n");
	daala_encode_free(context);
	return 0;
}