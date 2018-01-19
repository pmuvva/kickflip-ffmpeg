/*
 * Copyright (c) 2013, David Brodsky. All rights reserved.
 *
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <jni.h>
#include <android/log.h>
#include <string.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

#define LOG_TAG "FFmpegWrapper"
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGAV(...)  __android_log_print(ANDROID_LOG_INFO, "[FFmpeg_libAV]", __VA_ARGS__)

// Output
const char *outputPath;
const char *outputFormatName;
int hlsSegmentDurationSec = 10;
int audioStreamIndex = -1;
int videoStreamIndex = -1;
char *outputformat=NULL;

// Video
int VIDEO_PIX_FMT = AV_PIX_FMT_YUV420P;
int VIDEO_CODEC_ID = AV_CODEC_ID_H264;
int VIDEO_WIDTH = 1280;
int VIDEO_HEIGHT = 720;

// Audio
int AUDIO_CODEC_ID = AV_CODEC_ID_AAC;
int AUDIO_SAMPLE_FMT = AV_SAMPLE_FMT_S16;
int AUDIO_SAMPLE_RATE = 44100;
int AUDIO_CHANNELS = 1;

AVFormatContext *outputFormatContext;
AVStream *audioStream;
AVStream *videoStream;
AVCodec *audioCodec;
AVCodec *videoCodec;
AVRational *videoSourceTimeBase;
AVRational *audioSourceTimeBase;

AVPacket *packet; // recycled across calls to writeAVPacketFromEncodedData

// Example h264 file: Used to configure AVFormatContext
const char *sampleFilePath = "/sdcard/sample.ts";

// Debugging
int videoFrameCount = 0;
int WRITE_RAW_FILE = 0;		// Write raw video packets to file

FILE *raw_video;

// FFmpeg Utilities

void init(){
    av_register_all();
    avformat_network_init();
    avcodec_register_all();

    if(WRITE_RAW_FILE){
    	raw_video = fopen("/sdcard/raw.h264", "w");
    }
}

char* stringForAVErrorNumber(int errorNumber){
    char *errorBuffer = malloc(sizeof(char) * AV_ERROR_MAX_STRING_SIZE);

    int strErrorResult = av_strerror(errorNumber, errorBuffer, AV_ERROR_MAX_STRING_SIZE);
    if (strErrorResult != 0) {
        LOGE("av_strerror error: %d", strErrorResult);
        return NULL;
    }
    return errorBuffer;
}

//void addVideoStream(AVFormatContext *dest,uint8_t* data, int size){
void addVideoStream(AVFormatContext *dest){
    AVCodecContext *c_ctx_deprecated;
    AVCodecParameters *c_par;
	AVStream *st;
	AVCodec *codec;

	/* find the video encoder */
	if(!strcmp(outputformat,"hls"))
	    codec = avcodec_find_encoder(VIDEO_CODEC_ID);
	else
	    codec = avcodec_find_encoder(VIDEO_CODEC_ID);
	    //avcodec_find_encoder(dest->oformat->video_codec);

	if (!codec)
		LOGI("Destination video codec not found");
	else
	    LOGI("Destination video codec found. Name: %s, Long name: %s, AVCodecID: %d",
	        codec->name, codec->long_name, codec->id);

	st = avformat_new_stream(dest, codec);

	if (!st)
		LOGE("add_video_stream could not alloc stream");

	videoStreamIndex = st->index;

	LOGI("Added video stream context with index %d", videoStreamIndex);

	c_ctx_deprecated = st->codec;
	avcodec_get_context_defaults3(c_ctx_deprecated, codec);
	/* Some formats want stream headers to be separate. */
	if (dest->oformat->flags & AVFMT_GLOBALHEADER)
        c_ctx_deprecated->flags |= CODEC_FLAG_GLOBAL_HEADER;
    /* Using AVStream.codec to pass codec parameters to muxers is deprecated.
     *  The following also sets the parameters in AVStream.codecpar: */
    avcodec_parameters_from_context(st->codecpar, c_ctx_deprecated);

    c_par = st->codecpar;

	// The following shouldn't be necessary if the codec is found
    // But if the codec is *not* found, we force codec_id = VIDEO_CODEC_ID in
    // the codec context of the stream. This was the default behavior in the
    // original OpenWatch project
    if (!codec) {
        c_par->codec_id = VIDEO_CODEC_ID;
        LOGI("Stream codec context without codec, setting codec_id = %d", c_par->codec_id);
    }
    else
        LOGI("Stream codec context with codec_id: %d, codec_tag: %d",
            c_par->codec_id, c_par->codec_tag);

	/* Put sample parameters. */
	// c_par->bit_rate = 400000;
	/* Resolution must be a multiple of two. */
	c_par->width    = VIDEO_WIDTH;
	c_par->height   = VIDEO_HEIGHT;

	//c->pix_fmt = VIDEO_PIX_FMT;
	c_par->format = VIDEO_PIX_FMT;

   /* moved outside this function into prepareAdextraData
    c_par->extradata = (unsigned char *)malloc(size);
    for(int i=0;i<size;i++){
       c_par->extradata[i] =(int) data[i];
       LOGI("data[%d]: %x",i,(int)data[i]);
    }
    c_par->extradata_size = size;
    */

	/* timebase: This is the fundamental unit of time (in seconds) in terms
	 * of which frame timestamps are represented. For fixed-fps content,
	 * timebase should be 1/framerate and timestamp increments should be
	 * identical to 1. */

	/* Using AVStream.codec.time_base as a timebase hint to the muxer is deprecated.
	 * We now use AVStream.time_base instead. */
	st->time_base.den = 30;
    st->time_base.num = 1;

}


void addAudioStream(AVFormatContext *formatContext){
	AVCodecContext *codecContext;
	AVStream *st;
	AVCodec *codec;

	/* find the audio encoder */
	//codec = avcodec_find_encoder(AUDIO_CODEC_ID);
	if(!strcmp(outputformat,"hls")){
	    codec = avcodec_find_encoder(AUDIO_CODEC_ID);
	}else
	    //codec = avcodec_find_encoder(formatContext->oformat->audio_codec);
	    codec = avcodec_find_encoder(AUDIO_CODEC_ID);

	if (!codec) {
		LOGE("add_audio_stream codec not found");
	}
	else
	    LOGI("Audio codec found. Name: %s, Long name: %s, AVCodecID: %d", codec->name, codec->long_name, codec->id);


	st = avformat_new_stream(formatContext, codec);
	if (!st) {
		LOGE("add_audio_stream could not alloc stream");
	}

	audioStreamIndex = st->index;

	LOGI("Added audio stream at index: %d", audioStreamIndex);

	//st->id = 1;
	codecContext = st->codec;
	avcodec_get_context_defaults3(codecContext, codec);
	codecContext->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL; // for native aac support
	/* put sample parameters */
	//codecContext->sample_fmt  = AV_SAMPLE_FMT_FLT;
	codecContext->sample_fmt  = AUDIO_SAMPLE_FMT;

	/* Using AVStream.codec.time_base as a timebase hint to the muxer is deprecated.
     * We now use AVStream.time_base instead. */
	st->time_base.den = AUDIO_SAMPLE_RATE; //44100;
	st->time_base.num = 1;

	// NOTE: Added by Muvva
	//codecContext->frame_size = 1024;//AV_CODEC_CAP_VARIABLE_FRAME_SIZE;
    //st->duration = 1024;

	//c->bit_rate    = bit_rate;
	codecContext->sample_rate = AUDIO_SAMPLE_RATE;
	codecContext->channels    = AUDIO_CHANNELS;
	LOGI("addAudioStream, index: %d, sample_rate: %d, sample_fmt: %d, bit_rate: %lld", st->index,
	    codecContext->sample_rate, codecContext->sample_fmt, codecContext->bit_rate);
	// some formats want stream headers to be separate
	if (formatContext->oformat->flags & AVFMT_GLOBALHEADER)
		codecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;

	/* Using AVStream.codec to pass codec parameters to muxers is deprecated.
     *  The following also sets the parameters in AVStream.codecpar: */
    avcodec_parameters_from_context(st->codecpar, st->codec);
}



void copyAVFormatContext(AVFormatContext **dest, AVFormatContext **source){
    int numStreams = (*source)->nb_streams;
    LOGI("copyAVFormatContext source has %d streams", numStreams);
    int i;
    for (i = 0; i < numStreams; i++) {
        // Get input stream
        AVStream *inputStream = (*source)->streams[i];
        AVCodecContext *inputCodecContext = inputStream->codec;

        // Add new stream to output with codec from input stream
        //LOGI("Attempting to find encoder %s", avcodec_get_name(inputCodecContext->codec_id));
        AVCodec *outputCodec = avcodec_find_encoder(inputCodecContext->codec_id);
        if(outputCodec == NULL){
            LOGI("Unable to find encoder %s", avcodec_get_name(inputCodecContext->codec_id));
        }

			AVStream *outputStream = avformat_new_stream(*dest, outputCodec);
			AVCodecContext *outputCodecContext = outputStream->codec;

			// Copy input stream's codecContext for output stream's codecContext
			avcodec_copy_context(outputCodecContext, inputCodecContext);
			outputCodecContext->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;

			LOGI("copyAVFormatContext Copied stream %d with codec %s sample_fmt %s", i, avcodec_get_name(inputCodecContext->codec_id), av_get_sample_fmt_name(inputCodecContext->sample_fmt));
    }
}

// FFInputFile functions
// Using these to deduce codec parameters from test file

AVFormatContext* avFormatContextForInputPath(const char *inputPath, const char *inputFormatString){
    // You can override the detected input format
    AVFormatContext *inputFormatContext = NULL;
    AVInputFormat *inputFormat = NULL;
    //AVDictionary *inputOptions = NULL;

    if (inputFormatString) {
        inputFormat = av_find_input_format(inputFormatString);
        LOGI("avFormatContextForInputPath got inputFormat from string");
    }
    LOGI("avFormatContextForInputPath post av_Find_input_format");
    // It's possible to send more options to the parser
    // av_dict_set(&inputOptions, "video_size", "640x480", 0);
    // av_dict_set(&inputOptions, "pixel_format", "rgb24", 0);
    // av_dict_free(&inputOptions); // Don't forget to free

    LOGI("avFormatContextForInputPath pre avformat_open_input path: %s format: %s", inputPath, inputFormatString);
    int openInputResult = avformat_open_input(&inputFormatContext, inputPath, inputFormat, /*&inputOptions*/ NULL);
    LOGI("avFormatContextForInputPath avformat_open_input result: %d", openInputResult);
    if (openInputResult != 0) {
        LOGE("avformat_open_input failed: %s", stringForAVErrorNumber(openInputResult));
        avformat_close_input(&inputFormatContext);
        return NULL;
    }

    int streamInfoResult = avformat_find_stream_info(inputFormatContext, NULL);
    LOGI("avFormatContextForInputPath avformat_find_stream_info result: %d", streamInfoResult);
    if (streamInfoResult < 0) {
        avformat_close_input(&inputFormatContext);
        LOGE("avformat_find_stream_info failed: %s", stringForAVErrorNumber(openInputResult));
        return NULL;
    }

    LOGI("avFormatContextForInputPath Complete!");
    LOGI("AVInputFormat %s Stream0 codec: %s Stream1 codec: %s", inputFormatContext->iformat->name, avcodec_get_name(inputFormatContext->streams[0]->codec->codec_id), avcodec_get_name(inputFormatContext->streams[1]->codec->codec_id) );
    LOGI("Stream0 time_base: (num: %d, den: %d)", inputFormatContext->streams[0]->codec->time_base.num, inputFormatContext->streams[0]->codec->time_base.den);
    LOGI("Stream1 time_base: (num: %d, den: %d)", inputFormatContext->streams[1]->codec->time_base.num, inputFormatContext->streams[1]->codec->time_base.den);
    return inputFormatContext;
}

// FFOutputFile functions

AVFormatContext* avFormatContextForOutputPath(const char *path, const char *formatName){
    AVFormatContext *outputFormatContext;
    LOGI("avFormatContextForOutputPath format: %s path: %s", formatName, path);
    int openOutputValue = avformat_alloc_output_context2(&outputFormatContext, NULL, formatName, path);
    if (openOutputValue < 0) {
        avformat_free_context(outputFormatContext);
    }
    return outputFormatContext;
}

int openFileForWriting(AVFormatContext *avfc, const char *path){
    if (!(avfc->oformat->flags & AVFMT_NOFILE)) {
        LOGI("Opening output file for writing at path %s", path);
        return avio_open(&avfc->pb, path, AVIO_FLAG_WRITE);
    }
    return 0;		// This format does not require a file
}

int writeFileHeader(AVFormatContext *avfc){
    AVDictionary *options = NULL;

    // Write header for output file
    int writeHeaderResult = avformat_write_header(avfc, &options);
    if (writeHeaderResult < 0) {
        LOGE("Error writing header: %s", stringForAVErrorNumber(writeHeaderResult));
        av_dict_free(&options);
    }
    LOGI("Wrote file header");
    av_dict_free(&options);
    return writeHeaderResult;
}

int writeFileTrailer(AVFormatContext *avfc){
	if(WRITE_RAW_FILE){
		fclose(raw_video);
	}
    return av_write_trailer(avfc);
}

// Log callback

void av_log_callback(void *ptr, int level, const char *fmt, va_list vargs)
{
    // Recommend to leave this ON until we are completely sure muxing is working properly
    LOGAV(fmt, vargs);
}


  /////////////////////
  //  JNI FUNCTIONS  //
  /////////////////////

/*
 * Prepares an AVFormatContext for output.
 * Currently, the output format and codecs are hardcoded in this file.

 */

void Java_net_openwatch_ffmpegwrapper_FFmpegWrapper_prepareAVFormatContext(JNIEnv *env, jobject obj, jstring jOutputPath){
    init();

    // Create AVRational that expects timestamps in microseconds
    videoSourceTimeBase = av_malloc(sizeof(AVRational));
    videoSourceTimeBase->num = 1;
    videoSourceTimeBase->den = 1000000;

    audioSourceTimeBase = av_malloc(sizeof(AVRational));
	audioSourceTimeBase->num = 1;
	audioSourceTimeBase->den = 1000000;

    AVFormatContext *inputFormatContext;
    outputPath = (*env)->GetStringUTFChars(env, jOutputPath, NULL);

    if(!strcmp(outputformat,"hls")){
        outputFormatName = "hls";
        outputFormatContext = avFormatContextForOutputPath(outputPath, outputFormatName);
    }
    else
    {
        outputFormatName = "mp4";
        outputFormatContext = avFormatContextForOutputPath("-"/*PIPE mode*/, outputFormatName);
    }

    LOGI("post avFormatContextForOutputPath");

    av_log_set_level(AV_LOG_VERBOSE);
    av_log_set_callback(av_log_callback);
    LOGI("Current av_log_level = %d", av_log_get_level());

        addVideoStream(outputFormatContext);
        addAudioStream(outputFormatContext);
}


void Java_net_openwatch_ffmpegwrapper_FFmpegWrapper_prepareAdextraData(JNIEnv *env, jobject obj,jobject jData,jint jSize){

        uint8_t *data = (*env)->GetDirectBufferAddress(env, jData);

        // addVideoStream(outputFormatContext,data,(int)jSize);

        int numStreams = outputFormatContext->nb_streams;
        LOGI("outputFormatContext contains %d streams", numStreams);
        // find the video stream
        for (int k = 0; k < numStreams; k++) {
            // Get input stream
            LOGI("checking if stream %d is a video stream", k);
            AVStream *st = outputFormatContext->streams[k];
            AVCodecParameters *c = st->codecpar;
            if (c->codec_type == AVMEDIA_TYPE_VIDEO) {
                LOGI("Stream %d is media type video", k);
                c->extradata = (unsigned char *)malloc((int) jSize);
                for(int i = 0; i < (int)jSize; i++){
                    c->extradata[i] = (int) data[i];
                    LOGI("data[%d]: %x",i,(int)data[i]);
                }
                c->extradata_size = (int) jSize;
                //avcodec_parameters_from_context(st->codecpar, c);
            }
         }

        if(!strcmp(outputformat,"hls")){
            LOGI("WE ARE INSIDE OF HLS FORMAT outputFormatName:%s ",outputFormatName);
            LOGI("Segment duration will be set to %d seconds", hlsSegmentDurationSec);
            int res = av_opt_set_int(outputFormatContext->priv_data, "hls_time", hlsSegmentDurationSec, 0);
            LOGI("av_opt_set_int for setting hls_time returned %d", res);

            int result = openFileForWriting(outputFormatContext, outputPath);
            if(result < 0){
               LOGE("openFileForWriting error: %d", result);
            }
        }
        if(!strcmp(outputformat,"fmp4"))
        {
            LOGI("WE ARE INSIDE OF FMP4 FORMAT outputFormatName:%s ",outputFormatName);
            int res;
            // res = av_opt_set(outputFormatContext->priv_data, "movflags", "disable_chpl", 0);
            // LOGI("av_opt_set for movflags disable chpl returned %d", res);

            // Option frag_keyframe creates a fragment at every key-frame, which is overriden by the frag_duration optio
            //LOGI("Frag duration will be set to %d seconds", hlsSegmentDurationSec);
            //res = av_opt_set_int(outputFormatContext->priv_data, "frag_duration", 1000000*hlsSegmentDurationSec, 0);
            //LOGI("av_opt_set_int for setting frag_duration returned %d", res);

            res = av_opt_set(outputFormatContext->priv_data, "movflags", "empty_moov+default_base_moof+frag_keyframe+dash", 0);
            LOGI("av_opt_set for movflags returned %d", res);

            int result = dash_segmenter_avio_open(&outputFormatContext->pb,outputPath);
            if(result < 0){
               LOGE("dash_segmenter_avio_open error: %d", result);
               return;
            }
        /*
            int result = openFileForWriting(outputFormatContext, outputPath);
            if(result < 0){
               LOGE("openFileForWriting error: %d", result);
            }*/
        }

        writeFileHeader(outputFormatContext);

}

/*
 * Override default AV Options. Must be called before prepareAVFormatContext
 */

void Java_net_openwatch_ffmpegwrapper_FFmpegWrapper_setAVOptions(JNIEnv *env, jobject obj, jobject jOpts){
	// 1: Get your Java object's "jclass"!
	jclass ClassAVOptions = (*env)->GetObjectClass(env, jOpts);

	// 2: Get Java object field ids using the jclasss and field name as **hardcoded** strings!
	jfieldID jVideoHeightId = (*env)->GetFieldID(env, ClassAVOptions, "videoHeight", "I");
	jfieldID jVideoWidthId = (*env)->GetFieldID(env, ClassAVOptions, "videoWidth", "I");

	jfieldID jAudioSampleRateId = (*env)->GetFieldID(env, ClassAVOptions, "audioSampleRate", "I");
	jfieldID jNumAudioChannelsId = (*env)->GetFieldID(env, ClassAVOptions, "numAudioChannels", "I");

	jfieldID jHlsSegmentDurationSec = (*env)->GetFieldID(env, ClassAVOptions, "hlsSegmentDurationSec", "I");

	// 3: Get the Java object field values with the field ids!
	VIDEO_HEIGHT = (*env)->GetIntField(env, jOpts, jVideoHeightId);
	VIDEO_WIDTH = (*env)->GetIntField(env, jOpts, jVideoWidthId);

	AUDIO_SAMPLE_RATE = (*env)->GetIntField(env, jOpts, jAudioSampleRateId);
	AUDIO_CHANNELS = (*env)->GetIntField(env, jOpts, jNumAudioChannelsId);

	hlsSegmentDurationSec = (*env)->GetIntField(env, jOpts, jHlsSegmentDurationSec);

	// output format

    jfieldID joutput = (*env)->GetFieldID(env, ClassAVOptions, "outputFormatName", "Ljava/lang/String;");
    jstring joutmes = (*env)->GetObjectField(env, jOpts, joutput);
    outputformat = (*env)->GetStringUTFChars(env, joutmes, NULL);
	// that's how easy love can be!
}

/*
 * Consruct an AVPacket from MediaCodec output and call
 * av_interleaved_write_frame with our AVFormatContext
 */
void Java_net_openwatch_ffmpegwrapper_FFmpegWrapper_writeAVPacketFromEncodedData(JNIEnv *env, jobject obj, jobject jData, jint jIsVideo, jint jOffset, jint jSize, jint jFlags, jlong jPts,jint jkey){
    if(packet == NULL){
        packet = av_malloc(sizeof(AVPacket));
        LOGI("av_malloc packet");
    }

    if( ((int) jIsVideo) == JNI_TRUE ){
    	videoFrameCount++;
    }

    // jData is a ByteBuffer managed by Android's MediaCodec.
    // Because the audo track of the resulting output mostly works, I'm inclined to rule out this data marshaling being an issue
    uint8_t *data = (*env)->GetDirectBufferAddress(env, jData);

    if( WRITE_RAW_FILE && ((int) jIsVideo) == JNI_TRUE ){
    	fwrite(data, sizeof(uint8_t), (int)jSize, raw_video);
    }

    if(((int) jSize ) < 15){
    	if( ((int) jIsVideo) == JNI_TRUE ){
    		//LOGI("video: %d data: %s size: %d videoPacket#: %d", (int) jIsVideo, (char*)data, (int) jSize, videoFrameCount);
    	}else{
    		//LOGI("video: %d data: %s size: %d", (int) jIsVideo, data, (int) jSize);
    	}
    	//return;
    }

    av_init_packet(packet);

	if( ((int) jIsVideo) == JNI_TRUE){
		packet->stream_index = videoStreamIndex;
		if((int)jkey == JNI_TRUE){
		    packet->flags |= AV_PKT_FLAG_KEY;
		}
		//LOGI("writeAVPacket packet is for video stream with index: %d", packet->stream_index);
	}else{
		packet->stream_index = audioStreamIndex;
		//LOGI("writeAVPacket packet is for audio stream with index: %d", packet->stream_index);
		//packet->flags |= AV_PKT_FLAG_KEY;
	}

    packet->size = (int) jSize;
    packet->data = data;
    packet->pts = (int) jPts;

    //LOGI("writeAVPacket packet pts: %lld", packet->pts);
	packet->pts = av_rescale_q(packet->pts, *videoSourceTimeBase, (outputFormatContext->streams[packet->stream_index]->time_base));

    //LOGI("writeAVPacket packet info. frame count: %d, stream: %d, pts: %lld, size: %d",
    //    videoFrameCount, packet->stream_index, packet->pts, packet->size);

	/* Use this to break on specific frame */
	if(videoFrameCount == 3){
		//LOGI("break on frame");
		//LOGI("Payload size: %d", (int) jSize);
	}


    int writeFrameResult = av_interleaved_write_frame(outputFormatContext, packet);
    if(writeFrameResult < 0){
        LOGE("av_interleaved_write_frame video: %d pkt: %d size: %d error: %s", ((int) jIsVideo), videoFrameCount, ((int) jSize), stringForAVErrorNumber(writeFrameResult));
    }
    av_free_packet(packet);
}

/*
 * Finalize file. Basically a wrapper around av_write_trailer
 */
void Java_net_openwatch_ffmpegwrapper_FFmpegWrapper_finalizeAVFormatContext(JNIEnv *env, jobject obj){
    LOGI("finalizeAVFormatContext");
    int writeTrailerResult = writeFileTrailer(outputFormatContext);
    if(writeTrailerResult < 0){
        LOGE("av_write_trailer error: %d", writeTrailerResult);
    }
}


