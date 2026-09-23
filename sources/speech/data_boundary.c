#include "data_boundary.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static unsigned Read16(const unsigned char* p){return p[0]|((unsigned)p[1]<<8);}
static void Swap16(unsigned char* p){unsigned char a=p[0];p[0]=p[1];p[1]=a;}
static void Swap32(unsigned char* p){unsigned char a=p[0],b=p[1];p[0]=p[3];p[1]=p[2];p[2]=b;p[3]=a;}
void ShipyardSpeechPcmToNative(short* target,const unsigned char* source,size_t samples){
    size_t i;for(i=0;i<samples;++i)target[i]=(short)Read16(source+2*i);
}
int ShipyardSpeechFixupData(void* memory,size_t size,const char* name,const char* directory){
    unsigned char* p=(unsigned char*)memory;size_t i,j,offset;
    if(!strcmp(name,"phonindex")){
        if(size%2)return 0;for(i=0;i<size;i+=2)Swap16(p+i);return 1;
    }
    if(!strcmp(name,"phontab")){
        if(size<4)return 0;offset=4;
        for(i=0;i<p[0];++i){unsigned count;
            if(offset>size||size-offset<36)return 0;count=p[offset];offset+=36;
            if(count>(size-offset)/16)return 0;
            for(j=0;j<count;++j){Swap32(p+offset);Swap32(p+offset+4);Swap16(p+offset+8);offset+=16;}
        }
        return offset==size;
    }
    if(!strcmp(name,"intonations")){
        // TUNE contains only bytes except its final, reserved int.
        if(size%68)return 0;for(i=64;i<size;i+=68)Swap32(p+i);return 1;
    }
    if(!strcmp(name,"phondata")){
        char path[512],line[512];FILE* file;size_t end=8;unsigned sequences=0;
        if(size<8||strlen(directory)+20>=sizeof(path))return 0;
        sprintf(path,"%s/phondata-manifest",directory);file=fopen(path,"rb");if(!file)return 0;
        while(fgets(line,sizeof(line),file)){
            char type;unsigned address;unsigned count,stride;
            if(line[0]=='#'||line[0]=='\r'||line[0]=='\n')continue;
            if(sscanf(line," %c %x",&type,&address)!=2){fclose(file);return 0;}
            // W wave samples and E envelopes are explicitly byte-addressed by
            // wavegen/synthdata, as are phondata's version/sample-rate header.
            if(type!='S'){if(type!='W'&&type!='E'){fclose(file);return 0;}continue;}
            offset=address;if(offset<end||offset>size||size-offset<20){fclose(file);return 0;}
            count=p[offset+2];stride=(Read16(p+offset+4)&1)?64:44;
            if(count>(size-offset-4)/stride){fclose(file);return 0;}
            Swap16(p+offset);
            for(i=0;i<count;++i)for(j=0;j<16;j+=2)Swap16(p+offset+4+i*stride+j);
            end=offset+4+count*stride;
            ++sequences;
        }
        if(ferror(file)){fclose(file);return 0;}fclose(file);return sequences!=0;
    }
    return 0;
}
