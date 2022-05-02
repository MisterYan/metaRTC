﻿//
// Copyright (c) 2019-2022 yanggaofeng
//
#include <yangencoder/YangOpenH264Encoder.h>
#include <yangutil/yangavinfotype.h>
#include <yangutil/sys/YangLog.h>
#include <yangutil/sys/YangEndian.h>
#include <yangavutil/video/YangMeta.h>


YangOpenH264Encoder::YangOpenH264Encoder() {
	m_sendKeyframe=0;

	m_264Handle = NULL;
	m_yuvLen=0;
	 m_vlen=0;
	m_hasHeader=false;

	memset(&m_einfo,0,sizeof(SFrameBSInfo));
	memset(&m_pic,0,sizeof(SSourcePicture));


}

YangOpenH264Encoder::~YangOpenH264Encoder(void) {
	if (m_264Handle) {
		m_264Handle->Uninitialize();
		WelsDestroySVCEncoder (m_264Handle);
		m_264Handle = NULL;
	}
}
void YangOpenH264Encoder::sendKeyFrame(){
	m_sendKeyframe=1;
}

void YangOpenH264Encoder::setVideoMetaData(YangVideoMeta *pvmd) {

}

int32_t YangOpenH264Encoder::init(YangContext* pcontext) {
	if (m_isInit == 1)
		return Yang_Ok;

	YangVideoInfo* videoInfo=&pcontext->avinfo.video;
	YangVideoEncInfo* encInfo=&pcontext->avinfo.enc;
	setVideoPara(videoInfo, encInfo);
	int32_t width=videoInfo->outWidth;
	int32_t height=videoInfo->outHeight;
	m_yuvLen=width*height;
	m_vlen=m_yuvLen * 5 / 4;
	int ret = WelsCreateSVCEncoder(&m_264Handle);
		SEncParamExt eparam;
		m_264Handle->GetDefaultParams(&eparam);
		eparam.iUsageType = CAMERA_VIDEO_REAL_TIME;
		eparam.fMaxFrameRate = 150;
		eparam.iPicWidth = width;
		eparam.iPicHeight = height;
		eparam.iTargetBitrate = videoInfo->rate*1024;
		eparam.iRCMode = RC_BITRATE_MODE;
		eparam.iTemporalLayerNum = 1;
		eparam.iSpatialLayerNum = 1;
		eparam.bEnableDenoise = false;
		eparam.bEnableBackgroundDetection = true;
		eparam.bEnableAdaptiveQuant = false;
		eparam.bEnableFrameSkip = false;
		eparam.bEnableLongTermReference = false;
		eparam.uiIntraPeriod = 15u;
		eparam.eSpsPpsIdStrategy = CONSTANT_ID;
		eparam.bPrefixNalAddingCtrl = false;
		eparam.sSpatialLayers[0].iVideoWidth = width;
		eparam.sSpatialLayers[0].iVideoHeight = height;
		eparam.sSpatialLayers[0].fFrameRate = 64;
		eparam.sSpatialLayers[0].iSpatialBitrate = videoInfo->rate*1024;
		eparam.sSpatialLayers[0].iMaxSpatialBitrate = eparam.iMaxBitrate;
		int videoFormat = videoFormatI420;
		m_264Handle->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);
		m_264Handle->InitializeExt(&eparam);


		m_pic.iPicWidth = eparam.iPicWidth;
		m_pic.iPicHeight = eparam.iPicHeight;
		m_pic.iColorFormat = videoFormatI420;
		m_pic.iStride[0] = m_pic.iPicWidth;
		m_pic.iStride[1] = m_pic.iStride[2] = m_pic.iPicWidth / 2;

	m_isInit = 1;

	return Yang_Ok;

}

int32_t YangOpenH264Encoder::encode(YangFrame* pframe, YangEncoderCallback* pcallback) {

	//bool isKeyFrame = false;

	uint8_t* yuv_data=pframe->payload;
	int32_t destLength = 0;
	int32_t frametype = YANG_Frametype_P;
	if (m_sendKeyframe == 1) {
		m_sendKeyframe = 2;
		m_264Handle->ForceIntraFrame(true);

	}


	   // m_pic.uiTimeStamp = timestamp_++;
	    m_pic.pData[0] = yuv_data;
	    m_pic.pData[1] = yuv_data +m_yuvLen;
        m_pic.pData[2] = yuv_data + m_vlen;

	    //真正开始编码, encoded_frame_info类型为SFrameBSInfo, 用来获取编码后的NAL单元数据
	    int err = m_264Handle->EncodeFrame(&m_pic, &m_einfo);

	    if (err) {
	        yang_error("openh264 Encode err err=%d", err);
	        return 1;
	    }
	    if(videoFrameTypeIDR==m_einfo.eFrameType)  	frametype=YANG_Frametype_I;
	    //取数据方法,IDR帧会有2层,第一层为avc头,第二层为I帧数据
	    for (int i = 0; i < m_einfo.iLayerNum; ++i) {
	        SLayerBSInfo *pLayerBsInfo = &m_einfo.sLayerInfo[i];
	        int frameType = pLayerBsInfo->eFrameType;
	        if (pLayerBsInfo != NULL) {
	            int iLayerSize = 0;
	            //IDR帧,NAL数据也会有2个
	            int iNalIdx = pLayerBsInfo->iNalCount - 1;
	            do {
	                iLayerSize += pLayerBsInfo->pNalLengthInByte[iNalIdx];
	                --iNalIdx;
	            } while (iNalIdx >= 0);
	            memcpy(m_vbuffer + destLength,(char *) ((*pLayerBsInfo).pBsBuf),iLayerSize);
	            destLength+=iLayerSize;
	        }
	    }

	pframe->payload = frametype==YANG_Frametype_I?m_vbuffer:m_vbuffer+4;
	pframe->frametype = frametype;
	pframe->nb = frametype==YANG_Frametype_I?destLength:destLength-4;
	if(frametype==YANG_Frametype_I){
		int32_t spsLen=0,ppsLen=0,spsPos=0,ppsPos=0,ipos=0;
		spsPos=yang_find_pre_start_code(m_vbuffer,destLength);
		if(spsPos<0) return 1;
		ppsPos=yang_find_pre_start_code(m_vbuffer+4+spsPos,destLength-4-spsPos);
		if(ppsPos<0) return 1;
		ppsPos+=4+spsPos;
		ipos=yang_find_pre_start_code(m_vbuffer+4+ppsPos,destLength-4-ppsPos);
		if(ipos<0) return 1;
		ipos+=4+ppsPos;
		spsLen=ppsPos-spsPos-4;
		ppsLen=ipos-ppsPos-4;
		yang_put_be32((char*)m_vbuffer,(uint32_t)spsLen);
		yang_put_be32((char*)(m_vbuffer+4+spsLen),(uint32_t)ppsLen);

	}

	if (pcallback)
		pcallback->onVideoData(pframe);

	if (m_sendKeyframe == 2) {
		m_sendKeyframe = 0;
		yang_trace("\nsendkey.frametype==%d\n",	frametype);
	}

	return 1;
}


