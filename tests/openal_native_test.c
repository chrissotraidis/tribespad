/* Qualify the pinned native backend before connecting the original guest ABI. */
#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
static ALuint buffer,source;
static void create_tone(void){
 int16_t pcm[4096];for(unsigned i=0;i<4096;i++)pcm[i]=(int16_t)(1000*sin(i*6.283185307179586/64));
 alGenBuffers(1,&buffer);alBufferData(buffer,AL_FORMAT_MONO16,pcm,sizeof pcm,48000);alGenSources(1,&source);alSourcei(source,AL_BUFFER,buffer);alSourcei(source,AL_LOOPING,AL_TRUE);assert(alGetError()==AL_NO_ERROR);
}
static void cleanup(ALCdevice *device,ALCcontext *context){alSourceStop(source);alDeleteSources(1,&source);alDeleteBuffers(1,&buffer);assert(alGetError()==AL_NO_ERROR);assert(alcMakeContextCurrent(NULL));alcDestroyContext(context);assert(alcCloseDevice(device));}
static double measure(ALCdevice *device,float x,float z,double *left,double *right){
 float pcm[4096];alSource3f(source,AL_POSITION,x,0,z);alcRenderSamplesSOFT(device,pcm,2048);alcRenderSamplesSOFT(device,pcm,2048);assert(alcGetError(device)==ALC_NO_ERROR&&alGetError()==AL_NO_ERROR);
 *left=*right=0;for(unsigned i=0;i<2048;i++){*left+=pcm[2*i]*pcm[2*i];*right+=pcm[2*i+1]*pcm[2*i+1];}return *left+*right;
}
int main(void){
 assert(alcIsExtensionPresent(NULL,"ALC_SOFT_loopback"));ALCdevice *d=alcLoopbackOpenDeviceSOFT(NULL);assert(d);
 ALCint attrs[]={ALC_FORMAT_CHANNELS_SOFT,ALC_STEREO_SOFT,ALC_FORMAT_TYPE_SOFT,ALC_FLOAT_SOFT,ALC_FREQUENCY,48000,ALC_HRTF_SOFT,ALC_FALSE,0};
 ALCcontext *c=alcCreateContext(d,attrs);assert(c&&alcMakeContextCurrent(c));create_tone();alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);alSourcePlay(source);
 double l,r,l2,r2,far_l,far_r;double near=measure(d,-1,-1,&l,&r);measure(d,1,-1,&l2,&r2);double far=measure(d,8,-8,&far_l,&far_r);
 assert(near>0&&l>r*2&&r2>l2*2&&far<near/20);printf("PASS: native positional PCM left/right reversal and distance attenuation; energy %.8f -> %.8f\n",near,far);cleanup(d,c);
 /* The runner requires the CoreAudio backend, with no null/wave fallback. */
 d=alcOpenDevice(NULL);assert(d);c=alcCreateContext(d,NULL);assert(c&&alcMakeContextCurrent(c));printf("Native output: %s; renderer: %s\n",alcGetString(d,ALC_DEVICE_SPECIFIER),alGetString(AL_RENDERER));create_tone();alSourcef(source,AL_GAIN,.02f);alSourcePlay(source);
 ALint offset=0,state=0;for(unsigned i=0;i<50&&!offset;i++){usleep(10000);alGetSourcei(source,AL_SAMPLE_OFFSET,&offset);}alGetSourcei(source,AL_SOURCE_STATE,&state);assert(alGetError()==AL_NO_ERROR&&state==AL_PLAYING&&offset>0);printf("PASS: real CoreAudio playback advanced to sample %d\n",offset);cleanup(d,c);return 0;
}
