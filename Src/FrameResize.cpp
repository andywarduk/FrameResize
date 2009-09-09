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

#define ERR_OK		0
#define ERR_NODST	-1
#define ERR_NOTDDST	-2
#define ERR_NOSRC	-3
#define ERR_NOTDSRC	-4
#define ERR_PICWRERR	-5
#define ERR_FATAL	-6
#define ERR_NOTD	-7
#define ERR_ARGS	-8
#define ERR_DSTNF   -9

enum Orientation {
	Portrait,
	Landscape,
	Square
};

// Global Parameters
char *SrcDir;
char *DstDir;
char *EditDir=NULL;
bool Verbose=false;
int DstH;
int DstW;
char *Ext=NULL;
double FrameRatio;
Orientation FrameOrient;

int ScanDir(char *SrcDir,char *EditDir,char *DstDir);
int ProcessDirEnt(char *File,char *SrcDir,char *EditDir,char *DstDir);
int ProcessSrcFile(char *File,char *SrcDir,char *EditDir,char *DstDir);
int ProcessFile(char *Path,char *DstDir);
int FileMode(char *Path,mode_t *Mode);
bool SkipDir(char *Dir);
int CreateDirectory(char *Dir);
void Usage();
const char *OrientDesc(Orientation Orient);
time_t FileTime(struct stat *StatBuf);
void Perror(const char *Format,...);

void Usage()
{
	fprintf(stdout,"Usage: FrameResize [-v] [-e Ext] [ -s EditDir] SrcDir DstDir Dimensions\n"
                       "  where: -v         = Verbose messages\n"
                       "         -e Ext     = New file extension\n"
                       "         -s EditDir = Edited picture directory\n"
                       "         SrcDir     = Source picture directory\n"
                       "         DstDir     = Destination picture directory\n"
                       "         Dimensions = Target dimensions (wwwxhhh)\n");
}

int main(int ArgC, char **ArgV)
{
	int Result=ERR_OK;
	int Option;
	mode_t DirMode;
	char *Dims;
	char *XPos1,*XPos2;

	do{
		// Parse args
		while((Option=getopt(ArgC,ArgV,"ve:s:"))!=-1){
			switch(Option){
			case 'v':
				Verbose=true;
				break;
			case 'e':
				Ext=optarg;
				break;
			case 's':
				EditDir=optarg;
				break;
			default:
				fprintf(stderr,"Unrecognised option (%c)\n",Option);
				Result=ERR_ARGS;
				break;
			}
		}
		if(Result!=ERR_OK) break;

		// Need 3 args remaining
		if(ArgC-optind != 3){
			fputs("SrcDir, DstDir and Dimensions must be supplied\n",stderr);
			Result=ERR_ARGS;
			break;
		}

		// Get 3 remaining args
		SrcDir=ArgV[optind];
		DstDir=ArgV[optind+1];
		Dims=ArgV[optind+2];

		// Parse dimensions arg
		XPos1=strchr(Dims,'x');
		XPos2=strrchr(Dims,'x');
		if(XPos1==NULL || XPos1!=XPos2){
			fputs("Dimensions are invalid\n",stderr);
			Result=ERR_ARGS;
			break;
		}
		*XPos1='\x0';
		DstW=atoi(Dims);
		DstH=atoi(XPos1+1);

		// Check dimensions are sensible
		if(DstW<10 || DstH<10){
			fputs("Dimensions are too small\n",stderr);
			Result=ERR_ARGS;
			break;
		}

		// Work out frame ratio and orientation
		FrameRatio=(double) DstW / (double) DstH;
		if(FrameRatio==1.0) FrameOrient=Square;
		else if(FrameRatio<1.0) FrameOrient=Portrait;
		else FrameOrient=Landscape;

		// Dump what we are about to do
		if(Verbose){
			fprintf(stdout,"Converting %s to %s",SrcDir,DstDir);
			if(EditDir) fprintf(stdout," (edit directory %s)",EditDir);
			fprintf(stdout,", size %dx%d",DstW,DstH);
			if(Ext!=NULL) fprintf(stdout,", new ext %s",Ext);
			fputs("\n",stdout);
		}

		// Check dst dir exists and is a directory
		Result=FileMode(DstDir,&DirMode);
		if(Result!=ERR_OK){
			fprintf(stderr,"Destination directory %s does not exist\n",DstDir);
			Result=ERR_NODST;
			break;
		}
		if(!S_ISDIR(DirMode)){
			fprintf(stderr,"Destination %s is not a directory\n",DstDir);
			Result=ERR_NOTDDST;
			break;
		}

		// Check src dir exists and is a directory
		Result=FileMode(SrcDir,&DirMode);
		if(Result!=ERR_OK){
			fprintf(stderr,"Source directory %s does not exist\n",SrcDir);
			Result=ERR_NOSRC;
			break;
		}
		if(!S_ISDIR(DirMode)){
			fprintf(stderr,"Source %s is not a directory\n",SrcDir);
			Result=ERR_NOTDSRC;
			break;
		}

		if(EditDir){
			// Check edit dir exists and is a directory
			Result=FileMode(EditDir,&DirMode);
			if(Result!=ERR_OK){
				fprintf(stderr,"Edit directory %s does not exist\n",EditDir);
				Result=ERR_NOSRC;
				break;
			}
			if(!S_ISDIR(DirMode)){
				fprintf(stderr,"Edit %s is not a directory\n",EditDir);
				Result=ERR_NOTDSRC;
				break;
			}
		}

		// Process
		Result=ScanDir(SrcDir,EditDir,DstDir);
	} while(0);

	if(Result==ERR_ARGS){
		Usage();
	}

	exit(Result);
}

int ScanDir(char *SrcDir,char *EditDir,char *DstDir)
{
	int Result=ERR_OK;
	DIR *DirHandle;
	struct dirent *DirEnt;

	if(SkipDir(SrcDir)){
		if(Verbose) fprintf(stdout,"Skipping directory %s...\n",SrcDir);
	}
	else{
		if(Verbose) fprintf(stdout,"Scanning directory %s...\n",SrcDir);

		// Start reading directory contents
		DirHandle=opendir(SrcDir);
		if(DirHandle==NULL){
			Perror("Failed to open source directory %s",SrcDir);
			Result=errno;
		}
		else{
			while(true){
				// Get next directory entry
				DirEnt=readdir(DirHandle);
				if(DirEnt==NULL) break;

				// Skip "." and ".." entries
				if(strcmp(DirEnt->d_name,".")!=0 && strcmp(DirEnt->d_name,"..")!=0){
					// Process this file
					Result=ProcessDirEnt(DirEnt->d_name,SrcDir,EditDir,DstDir);
					if(Result!=ERR_OK) break;
				}
			}

			// End directory scan
			closedir(DirHandle);
		}
	}

	return Result;
}

int ProcessDirEnt(char *File,char *SrcDir,char *EditDir,char *DstDir)
{
	int Result=ERR_OK;
	char FilePath[PATH_MAX+1];
	char NewEditDir[PATH_MAX+1];
	char NewDstDir[PATH_MAX+1];
	mode_t Mode;

	// Build full path to source
	sprintf(FilePath,"%s/%s",SrcDir,File);

	// Get file mode
	Result=FileMode(FilePath,&Mode);
	if(Result!=ERR_OK){
		// Failed
		Perror("Failed to stat %s",FilePath);
	}
	else{
		// Check file type
		if(S_ISREG(Mode)){
			// Regular file
			Result=ProcessSrcFile(File,SrcDir,EditDir,DstDir);
		}
		else if(S_ISDIR(Mode)){
			// Directory
			sprintf(NewEditDir,"%s/%s",EditDir,File);
			sprintf(NewDstDir,"%s/%s",DstDir,File);
			Result=ScanDir(FilePath,NewEditDir,NewDstDir);
		}
		else{
			if(Verbose) fprintf(stderr,"Skipping %s",FilePath);
		}
	}

	return Result;
}

int ProcessSrcFile(char *File,char *SrcDir,char *EditDir,char *DstDir)
{
	int Result=ERR_OK;
	char SrcPath[PATH_MAX+1];
	char DstPath[PATH_MAX+1];
	struct stat SrcStatBuf;
	struct stat DstStatBuf;
	char *FName;
	char *ExtPos;
	mode_t Mode;

	do{
		// Find and lstat source file
		do{
			if(EditDir){
				// Look in edit directory first
				sprintf(SrcPath,"%s/%s",EditDir,File);
				if(lstat(SrcPath,&SrcStatBuf)==0){
					// Got edited file
					break;
				}
				else{
					switch(errno){
					case ENOENT:
						// Does not exist
						break;
					default:
						Result=errno;
						Perror("Failed to stat %s",SrcPath);
					}
					if(Result!=ERR_OK) break;
				}
			}

			// Look in source directory
			sprintf(SrcPath,"%s/%s",SrcDir,File);
			if(lstat(SrcPath,&SrcStatBuf)!=0){
				// This shouldn't fail as we've already lstatted it in ProcessDirEnt
				Result=errno;
				Perror("Failed to stat %s",SrcPath);
			}
		} while(0);

		if(Result!=ERR_OK) break;

		// Calculate destination path
		sprintf(DstPath,"%s/%s",DstDir,File);

		// Need to replace extension?
		if(Ext!=NULL){
			// Get pointer to file name
			FName=strrchr(DstPath,'/');
			if(FName==NULL) FName=DstPath;
			else ++FName;

			// Find extension within filename
			ExtPos=strrchr(FName,'.');
			if(ExtPos==NULL){
				ExtPos=DstPath+strlen(DstPath);
			}
			sprintf(ExtPos,".%s",Ext);
		}

		if(lstat(DstPath,&DstStatBuf)==0){
			// File already exists
			if(S_ISREG(DstStatBuf.st_mode)){
				if(FileTime(&DstStatBuf)>FileTime(&SrcStatBuf)){
					if(Verbose) fprintf(stdout,"%s newer than %s. skipping\n",DstPath,SrcPath);
					break;
				}
			}
			else{
				fprintf(stderr,"%s exists but isn't a regular file\n",DstPath);
				Result=ERR_DSTNF;
				break;
			}
		}
		else{
			switch(errno){
			case ENOENT:
				// Does not exist
				break;
			default:
				Result=errno;
				Perror("Failed to stat %s",DstPath);
			}
			if(Result!=ERR_OK) break;
		}

		// TODO Check if already in skipped list?

		// Make sure parent directory is created
		Result=FileMode(DstDir,&Mode);
		switch(Result){
		case ENOENT:
			// Create destination directory
			Result=CreateDirectory(DstDir);
			if(Result!=ERR_OK){
				fprintf(stderr,"Error creating directory %s\n",DstDir);
			}
			break;
		case 0:
			if(!S_ISDIR(Mode)){
				fprintf(stderr,"  Output directory %s already exists and is not a directory\n",DstDir);
				Result=ERR_NOTDDST;
			}
			break;
		default:
			Perror("Failed to stat %s",DstDir);
			break;
		}
		if(Result!=ERR_OK) break;

		ProcessFile(SrcPath,DstPath);
	} while(0);

	return Result;
}

int ProcessFile(char *SrcPath,char *DstPath)
{
	int Result=ERR_OK;
	Image Picture;
	double XScale,YScale;
	double ImageRatio;
	Orientation ImageOrient;
	unsigned int TargetSize;
	Geometry Geom;

	do{
		if(Verbose) fprintf(stdout,"Processing file %s...\n",SrcPath);

		// Read file
		try{
			Picture.read(SrcPath);
		}
		catch(Error& ReadError)
		{
			fprintf(stderr,"Error loading %s (%s)\n",SrcPath,ReadError.what());
			break;
		}

		// Check image has an image type filled in (empty files exhibit this behaviour)
		if(strcmp(Picture.magick().c_str(),"")==0){
			fprintf(stdout,"Skipping %s (image type could not be determined)\n",SrcPath);
			break;
		}

		switch(Picture.orientation()){
		case TopRightOrientation: // 2, flipped horizontally
			if(Verbose) fprintf(stdout,"  flipping horizontally to normalise orientation\n");
			Picture.flop();
			break;
		case BottomRightOrientation: // 3, rotated 180
			if(Verbose) fprintf(stdout,"  flipping horizontally and vertically (180 rotation) to normalise orientation\n");
			Picture.flip();
			Picture.flop();
			break;
		case BottomLeftOrientation: // 4, flipped vertically
			if(Verbose) fprintf(stdout,"  flipping vertically to normalise orientation\n");
			Picture.flip();
			break;
		case LeftTopOrientation: // 5, - orient changed
			if(Verbose) fprintf(stdout,"  rotating 270 to normalise orientation\n");
			Picture.rotate(270);
			Picture.flip();
			break;
		case RightTopOrientation: // 6, rotated 90 - orient changed
			if(Verbose) fprintf(stdout,"  rotating 90 to normalise orientation\n");
			Picture.rotate(90);
			break;
		case RightBottomOrientation: // 7, - orient changed
			if(Verbose) fprintf(stdout,"  rotating 90 and flipping vertically to normalise orientation\n");
			Picture.rotate(90);
			Picture.flip();
			break;
		case LeftBottomOrientation: // 8, rotated 270 - orient changed
			if(Verbose) fprintf(stdout,"  rotating 270 to normalise orientation\n");
			Picture.rotate(270);
			break;
		default: // Assume TopLeftOrientation here
			break;
		}
		Picture.orientation(TopLeftOrientation);

		// Work out image orientation
		XScale = Picture.xResolution() * Picture.columns();
		YScale = Picture.yResolution() * Picture.rows();

		ImageRatio=XScale / YScale;
		if(ImageRatio==1.0) ImageOrient=Square;
		else if(ImageRatio<1.0) ImageOrient=Portrait;
		else ImageOrient=Landscape;

		// Dump image characteristics
		if(Verbose) fprintf(stdout,"  Size=%dx%d (%.1f MP), Resolution=%.0fx%.0f, Orientation=%s, Type=%s\n",Picture.columns(),Picture.rows(),
			((double) Picture.columns() * (double) Picture.rows()) / 1000000.0,
			Picture.xResolution(),Picture.yResolution(),
			OrientDesc(ImageOrient),
			Picture.magick().c_str());

		// Check image orientation is compatible
		if(FrameOrient!=Square && ImageOrient!=Square && ImageOrient!=FrameOrient){
			fprintf(stdout,"Skipping %s (image is wrong orientation for frame)\n",SrcPath);
			break;
		}

		if(Verbose) fprintf(stdout,"  Outputting to file %s\n",DstPath);

		// Crop the image
		if(ImageRatio!=FrameRatio){
			Geom=Geometry();
			if(ImageRatio<FrameRatio){
				// Work out how many rows to chop out
				TargetSize=(int)((double) Picture.columns() / FrameRatio);
				Geom.width(Picture.columns());
				Geom.height(TargetSize);
				Geom.xOff(0);
				Geom.yOff((Picture.rows()-TargetSize)/2);
			}
			else{
				// Work out how many cols to chop out
				TargetSize=(int)((double) Picture.rows() * FrameRatio);
				Geom.width(TargetSize);
				Geom.height(Picture.rows());
				Geom.xOff((Picture.columns()-TargetSize)/2);
				Geom.yOff(0);
			}
			if(Verbose) fprintf(stdout,"  Cropping to %dx%d at %dx%d\n",Geom.width(),Geom.height(),Geom.xOff(),Geom.yOff());
			Picture.crop(Geom);
		}

		// Zoom the image
		Geom=Geometry(DstW,DstH);
		Geom.aspect(true); // Ignore original aspect ratio
		Picture.zoom(Geom);

		// Save the image
		try{
			Picture.write(DstPath);
		}
		catch(Error& WriteError)
		{
			fprintf(stderr,"Error writing %s (%s)\n",DstPath,WriteError.what());
			Result=ERR_PICWRERR;
			break;
		}
	} while(0);

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
	int Result=ERR_OK;
	char Parent[NAME_MAX];
	char *ParentPtr;
	mode_t Mode;

	if(FileMode(Dir,&Mode)!=ERR_OK){
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
	int Result=ERR_OK;
	struct stat StatBuf;

	Result=lstat(Path,&StatBuf);
	if(Result==ERR_OK){
		*Mode=StatBuf.st_mode;
	}
	else{
		Result=errno;
	}

	return Result;
}

const char *OrientDesc(Orientation Orient)
{
	switch(Orient){
	case Portrait:
		return "Portrait";
	case Landscape:
		return "Landscape";
	case Square:
		return "Square";
	}

	return "Unknown";
}

time_t FileTime(struct stat *StatBuf)
{
	time_t Result;

	Result=StatBuf->st_mtime;
	if(StatBuf->st_ctime>Result){
		Result=StatBuf->st_ctime;
	}

	return Result;
}

void Perror(const char *Format,...)
{
	va_list Args;

	va_start(Args,Format);
	vfprintf(stderr,Format,Args);
	va_end(Args);
	fputs(": ",stderr);
	perror(NULL);
}
