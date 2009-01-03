#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <Magick++.h>

using namespace Magick;

#define ERR_NODST	-1
#define ERR_NOTDDST	-2
#define ERR_NOSRC	-3
#define ERR_NOTDSRC	-4
#define ERR_PICWRERR	-5
#define ERR_FATAL	-6
#define ERR_NOTD	-7
#define ERR_ARGS	-8
#define ERR_BREAK	-100

char *SrcDir;
char *DstDir;
bool Verbose=false;
int DstH;
int DstW;

int FileMode(char *Path,mode_t *Mode);
int ProcessFile(char *Path,char *DstDir);
int ScanDir(char *SrcDir,char *DstDir);
bool SkipDir(char *Dir);
int CreateDirectory(char *Dir);
void Usage();

void Usage()
{
	fprintf(stdout,"Usage: FrameResize [-v] SrcDir DstDir Dimensions\n"
                       "  where: -v         = Verbose messages\n"
                       "         SrcDir     = Source picture directory\n"
                       "         DstDir     = Destination picture directory\n"
                       "         Dimensions = Target dimensions (wwwxhhh)\n");
}

int main(int ArgC, char **ArgV)
{
	int Result=0;
	int Option;
	mode_t DirMode;
	char *Dims;
	char *XPos1,*XPos2;

	do{
		while((Option=getopt(ArgC,ArgV,"v"))!=-1){
			switch(Option){
			case 'v':
				Verbose=true;
				break;
			default:
				Result=ERR_ARGS;
				break;
			}
		}
		if(Result!=0) break;

		if(ArgC-optind != 3){
			Result=ERR_ARGS;
			break;
		}

		SrcDir=ArgV[optind];
		DstDir=ArgV[optind+1];
		Dims=ArgV[optind+2];

		XPos1=strchr(Dims,'x');
		XPos2=strrchr(Dims,'x');
		if(XPos1==NULL || XPos1!=XPos2){
			Result=ERR_ARGS;
			break;
		}
		*XPos1='\x0';
		DstW=atoi(Dims);
		DstH=atoi(XPos1+1);

		if(DstW<10 || DstH<10){
			Result=ERR_ARGS;
			break;
		}

		if(Verbose) fprintf(stdout,"Converting %s to %s, size %dx%d\n",SrcDir,DstDir,DstW,DstH);

		// Check dst dir exists
		Result=FileMode(DstDir,&DirMode);
		if(Result!=0){
			fprintf(stderr,"Destination directory %s does not exist\n",DstDir);
			Result=ERR_NODST;
			break;	
		}
		if(!S_ISDIR(DirMode)){
			fprintf(stderr,"Destination %s is not a directory\n",DstDir);
			Result=ERR_NOTDDST;
			break;	
		}
		
		// Check src dir exists
		Result=FileMode(SrcDir,&DirMode);
		if(Result!=0){
			fprintf(stderr,"Source directory %s does not exist\n",SrcDir);
			Result=ERR_NOSRC;
			break;	
		}
		if(!S_ISDIR(DirMode)){
			fprintf(stderr,"Source %s is not a directory\n",SrcDir);
			Result=ERR_NOTDSRC;
			break;	
		}

		Result=ScanDir(SrcDir,DstDir);
	} while(0);

	if(Result==ERR_ARGS){
		Usage();
	}

	exit(Result);
}

int ScanDir(char *SrcDir,char *DstDir)
{
	int Result=0;
	DIR *DirHandle;
	struct dirent *DirEnt;
	char FilePath[PATH_MAX+1];
	char NewDstDir[PATH_MAX+1];
	mode_t Mode;

	if(SkipDir(SrcDir)){
		if(Verbose) fprintf(stdout,"Skipping directory %s...\n",SrcDir);
	}
	else{
		if(Verbose) fprintf(stdout,"Scanning directory %s...\n",SrcDir);
		DirHandle=opendir(SrcDir);
		if(DirHandle==NULL){
			perror("opendir");
			Result=errno;
		}
		else{
			while(true){
				DirEnt=readdir(DirHandle);
				if(DirEnt==NULL){
					break;
				}
				if(strcmp(DirEnt->d_name,".")!=0 && strcmp(DirEnt->d_name,"..")!=0){
					sprintf(FilePath,"%s/%s",SrcDir,DirEnt->d_name);
					Result=FileMode(FilePath,&Mode);
					if(Result!=0){
						perror("lstat");
						break;
					}
					if(S_ISREG(Mode)){
						Result=ProcessFile(FilePath,DstDir);
					}
					else if(S_ISDIR(Mode)){
						sprintf(NewDstDir,"%s/%s",DstDir,DirEnt->d_name);
						Result=ScanDir(FilePath,NewDstDir);
					}
					else{
						if(Verbose) fprintf(stderr,"Skipping %s",FilePath);
					}
					if(Result!=0) break;
				}
			}
			closedir(DirHandle);
		}
	}
	
	return Result;
}

int ProcessFile(char *Path,char *DstDir)
{
	int Result=0;
	Image Picture;
	char OutPath[NAME_MAX];
	char *FName;
	mode_t Mode;
	double XScale,YScale;
	bool FrameLandscape;
	bool Landscape;
	double TargetRatio;
	unsigned int TargetSize;
	Geometry Geom;

	do{
		if(Verbose) fprintf(stdout,"Processing file %s...\n",Path);
		if(Verbose) fprintf(stdout,"  Outputting to directory %s\n",DstDir);
	
		// Get pointer to file name
		FName=strrchr(Path,'/');
		if(FName==NULL){
			fprintf(stderr,"Unable to determine filename from %s\n",Path);
			Result=ERR_FATAL;
			break;
		}
		++FName;

		// Build destination name
		sprintf(OutPath,"%s/%s",DstDir,FName);

		// Check destination doesn't already exist
		Result=FileMode(OutPath,&Mode);
		switch(Result){
		case 0:
			if(Verbose){
				fprintf(stdout,"  Output file %s already exists\n",OutPath);
				Result=ERR_BREAK;
			}
			break;
		case ENOENT:
			Result=0;
			break;
		default:
			perror("lstat");
			break;
		}
		if(Result!=0) break;

		// Read file
		try{
			Picture.read(Path);
		}
		catch(Error& ReadError)
		{
			fprintf(stderr,"Error loading %s (%s)\n",Path,ReadError.what());
			break;
		}

		// Check image has an image type filled in (empty files exhibit this behaviour)
		if(strcmp(Picture.magick().c_str(),"")==0){
			fprintf(stdout,"Skipping %s (image type could not be determined)\n",Path);
			break;
		}

		// Work out image orientation
		XScale=Picture.xResolution()*Picture.columns();
		YScale=Picture.xResolution()*Picture.rows();

		if(XScale<=YScale){
			Landscape=false;
		}
		else{
			Landscape=true;
		}

		if(DstW<=DstH){
			FrameLandscape=false;
		}
		else{
			FrameLandscape=true;
		}

		if(Verbose) fprintf(stdout,"  Size=%dx%d (%.1f MP), Resolution=%.0fx%.0f, Orientation=%s, Type=%s\n",Picture.columns(),Picture.rows(),
			((double) Picture.columns() * (double) Picture.rows()) / 1000000.0,
			Picture.xResolution(),Picture.yResolution(),
			(Landscape?"Landscape":"Portrait"),
			Picture.magick().c_str());
		
		if(Landscape!=FrameLandscape){
			fprintf(stdout,"Skipping %s (image is wrong orientation for frame)\n",Path);
			break;
		}

		// Check destination directory exists
		Result=FileMode(DstDir,&Mode);
		switch(Result){
		case ENOENT:
			// Create destination directory
			Result=CreateDirectory(DstDir);
			if(Result!=0){
				fprintf(stderr,"Error creating directory %s\n",DstDir);
			}
			break;
		case 0:
			if(!S_ISDIR(Mode)){
				if(Verbose) fprintf(stdout,"  Output directory %s already exists and is not a directory\n",DstDir);
				Result=ERR_NOTDDST;
			}
			break;
		default:
			perror("lstat");
			break;
		}
		if(Result!=0) break;
		
		if(Verbose) fprintf(stdout,"  Outputting to file %s\n",OutPath);

		// Crop the image
		TargetRatio=(double) DstH / (double) DstW;
		Geom=Geometry();
		if(Landscape){
			// Work out how many rows to chop out
			TargetSize=(int)((double) Picture.columns() * TargetRatio);
			Geom.width(Picture.columns());
			Geom.height(TargetSize);
			Geom.xOff(0);
			Geom.yOff((Picture.rows()-TargetSize)/2);
		}
		else{
			// Work out how many cols to chop out
			TargetSize=(int)((double) Picture.rows() / TargetRatio);
			Geom.width(TargetSize);
			Geom.height(Picture.rows());
			Geom.xOff((Picture.columns()-TargetSize)/2);
			Geom.yOff(0);
		}
		if(Verbose) fprintf(stdout,"  Cropping to %dx%d at %dx%d\n",Geom.width(),Geom.height(),Geom.xOff(),Geom.yOff());
		Picture.crop(Geom);

		// Zoom the image
		Geom=Geometry(DstW,DstH);
		Geom.aspect(true);
		Picture.zoom(Geom);

		// Save the image
		try{
			Picture.write(OutPath);
		}
		catch(Error& WriteError)
		{
			fprintf(stderr,"Error writing %s (%s)\n",OutPath,WriteError.what());
			Result=ERR_PICWRERR;
			break;
		}
	} while(0);

	if(Result==ERR_BREAK) Result=0;

	return Result;
}

bool SkipDir(char *Dir)
{
	bool Skip=false;
	char TokenPath[NAME_MAX];
	mode_t Mode;

	if(strcmp(Dir,DstDir)==0) Skip=true;
	else{
		sprintf(TokenPath,"%s/.FrameSkip",Dir);
		if(FileMode(TokenPath,&Mode)==0) Skip=true;
	}

	return Skip;
}

int CreateDirectory(char *Dir)
{
	int Result=0;
	char Parent[NAME_MAX];
	char *ParentPtr;
	mode_t Mode;

	if(FileMode(Dir,&Mode)!=0){
		strcpy(Parent,Dir);
		ParentPtr=strrchr(Parent,'/');
		if(ParentPtr){
			*ParentPtr='\x0';
			Result=CreateDirectory(Parent);
			if(Result==0){
				if(Verbose) fprintf(stdout,"Creating directory %s\n",Dir);
				Result=mkdir(Dir,0777);
			}
		}
		else{
			Result=ERR_FATAL;
		}
	}
	else if(!S_ISDIR(Mode)){
		Result=ERR_NOTD;
	}

	return Result;
}

int FileMode(char *Path,mode_t *Mode)
{
	int Result=0;
	struct stat StatBuf;

	Result=lstat(Path,&StatBuf);
	if(Result==0){
		*Mode=StatBuf.st_mode;
	}
	else{
		Result=errno;
	}

	return Result;
}
