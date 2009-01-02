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
#define ERR_BREAK	-100

// TODO parameterise
char *SrcDir="/home/ajw/Src/FrameResize/Test/Src";
char *DstDir="/home/ajw/Src/FrameResize/Test/Dst";
bool Verbose=true;
int DstH=480;
int DstW=800;

int FileMode(char *Path,mode_t *Mode);
int ProcessFile(char *Path,char *DstDir);
int ScanDir(char *SrcDir,char *DstDir);
int CreateDirectory(char *Dir);

int main(int ArgC, char **ArgV)
{
	int Result=0;
	mode_t DirMode;

	do{
//		getopt();

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

	if(Verbose) fprintf(stdout,"Scanning directory %s...\n",SrcDir);
	DirHandle=opendir(SrcDir);
	if(DirHandle==NULL){
		perror("opendir");
		Result=10;
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
	int Orientation;
	bool FileLandscape;
	bool DispLandscape;
	double TargetRatio;
	int TargetSize;
	int CropW;
	int CropH;
	int CropX;
	int CropY;

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
		Orientation=atoi(Picture.attribute("EXIF:Orientation").c_str());

		XScale=Picture.xResolution()*Picture.columns();
		YScale=Picture.xResolution()*Picture.rows();

		if(XScale<=YScale){
			FileLandscape=false;
		}
		else{
			FileLandscape=true;
		}

		switch(Orientation){
		case 5:
		case 6:
		case 7:
		case 8:
			// Exif tag triggers rotation
			DispLandscape=!FileLandscape;
			break;
		default:
			DispLandscape=FileLandscape;
			break;
		}

		if(Verbose) fprintf(stdout,"  Size=%dx%d (%.2f MP), Resolution=%.0fx%.0f, Orientation=%d, Type=%s\n",Picture.columns(),Picture.rows(),
			((double) Picture.columns() * (double) Picture.rows()) / 1000000.0,
			Picture.xResolution(),Picture.yResolution(),
			Orientation,
			Picture.magick().c_str());
		
		if(!DispLandscape){
			fprintf(stdout,"Skipping %s (image is portrait)\n",Path);			
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
		if(FileLandscape){
			// Work out how many rows to chop out
			TargetRatio=(double) DstH / (double) DstW;
			TargetSize=(int)((double) Picture.columns() * TargetRatio);
			CropW=Picture.columns();
			CropH=TargetSize;
			CropX=0;
			CropY=(Picture.rows()-TargetSize)/2;
		}
		else{
			// Work out how many cols to chop out
			TargetRatio=(double) DstW / (double) DstH;
			TargetSize=(int)((double) Picture.rows() * TargetRatio);
			CropW=TargetSize;
			CropH=Picture.rows();
			CropX=(Picture.columns()-TargetSize)/2;
			CropY=0;
		}
		if(Verbose) fprintf(stdout,"  Cropping to %dx%d at %dx%d\n",CropW,CropH,CropX,CropY);
		Picture.crop(Geometry(CropW,CropH,CropX,CropY));

		// Zoom the image
		Picture.zoom(Geometry(DstW,DstH));

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
