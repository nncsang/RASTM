#include <iostream>
#include "cv.h"
#include "highgui.h"
#include <fstream>
#include <time.h>

using namespace std;
using namespace cv;

#define maxBlog 1024		// số blog tối đa của một template
#define maxTempl 8			// số template tối đa tương ứng một object nên là bội của 2 ^ n
#define nObj 1			// số object tối đa
#define nScale 7			// số scale hỗ trợ
#define nScale_Opt	8
#define maxH 512
#define maxW 512
#define thresholdMatch 0.8	// threshold cho đoạn matching
#define hBlog 5				// 
#define wBlog 5				// 3 thông số của blog gồm chiều cao, chiều rộng, và threshold cho số pixel thuộc đối tượng của blog
#define thresholdBolg 15	//	
#define disOrient 360		// độ dài mỗi bin

struct Blog
{
	int topLeft_x;			//
	int topLeft_y;			// tọa độ của blog
	int botRight_x;			//
	int botRight_y;			// 

	int sum;				// tổng giá trị các pixel trong một blog
};

int scaleFactor[nScale] = {100, 90, 80, 70, 60, 50, 40}; //{100, 75, 50};;//	// giá trị các scale
int limit = -1;							// +oo vì giá trị correlation >= -1, <= 1

IplImage** templ;						// mảng ảnh các templ
IplImage** pyrTempl;					// mảng ảnh các templ đã down scale 50%

IplImage** mask;						// mảng ảnh các mặt nạ
IplImage** pyrMask;						// mảng ảnh các mặt nạ đã down scale 50%

int* nBlog;								// nBlog[i] là số blog của template i (đã down scale 50%)
Blog** bg;								// bg[i] là mảng các blog của template i (đã down scale 50%)

int* nPoint;							//
int** pt_X;								// các điểm cần quan tâm của template i	
int** pt_Y;								//

int* nPoint_pyr;						//
int** pt_pyr_X;							// các điểm cần quan tâm của template i (đã down scale 50%)
int** pt_pyr_Y;							//

int nTempl = 0;							// số template

void DynamicAllocation()
{
	int nOri = 360 / disOrient;
	int cTempl = nScale_Opt * nObj * nOri;
	int maxPoint = maxH * maxW;

	templ = new IplImage*[cTempl];
	pyrTempl = new IplImage*[cTempl];

	mask = new IplImage*[cTempl];
	pyrMask = new IplImage*[cTempl];

	bg = new Blog*[cTempl];

	nPoint = new int[cTempl];
	nBlog = new int[cTempl];
	nPoint_pyr = new int[cTempl];

	pt_X = new int*[cTempl];
	#pragma omp parallel for num_threads(cvGetNumThreads()) schedule(dynamic)
	{
		for(int i = 0; i < cTempl; i++)
			pt_X[i] = new int[maxPoint];
	}

	pt_Y = new int*[cTempl];
	#pragma omp parallel for num_threads(cvGetNumThreads()) schedule(dynamic)
	{
		for(int i = 0; i < cTempl; i++)
			pt_Y[i] = new int[maxPoint];
	}

	pt_pyr_X = new int*[cTempl];
	#pragma omp parallel for num_threads(cvGetNumThreads()) schedule(dynamic)
	{
		for(int i = 0; i < cTempl; i++)
			pt_pyr_X[i] = new int[maxPoint / 2];
	}

	pt_pyr_Y = new int*[cTempl];
	#pragma omp parallel for num_threads(cvGetNumThreads()) schedule(dynamic)
	{
		for(int i = 0; i < cTempl; i++)
			pt_pyr_Y[i] = new int[maxPoint / 2];
	}
}

void Reset()
{
	nTempl = 0;
	for(int i = 0; i < nTempl; i++)
	{
		cvReleaseImage(&templ[i]);
		cvReleaseImage(&pyrTempl[i]);
		cvReleaseImage(&mask[i]);
		cvReleaseImage(&pyrMask[i]);
	}
}

void Release()
{
	int nOri = 360 / disOrient;
	int cTempl = nScale_Opt * nObj * nOri;
	int maxPoint = maxH * maxW;

	#pragma omp parallel for num_threads(cvGetNumThreads()) schedule(dynamic)
	{
		for(int i = 0; i < nTempl; i++)
		{
			delete bg[i];
			delete pt_X[i];
			delete pt_Y[i];

			delete pt_pyr_X[i];
			delete pt_pyr_Y[i];
		}
	}

	delete templ;
	delete pyrTempl;
	delete mask;
	delete pyrMask;
	delete bg;
	delete nPoint;
	delete nBlog;
	delete nPoint_pyr;
	delete pt_X;
	delete pt_Y;
	delete pt_pyr_X;
	delete pt_pyr_Y;
}

inline void swap(double& a, double& b)
{
	double tmp;
	tmp = a;
	a = b;
	b = tmp;
}

inline void swap(int& a, int& b)
{
	int tmp;
	tmp = a;
	a = b;
	b = tmp;
}

void QuickSortStack(double a[], int x[], int y[], int n)	
{
	int sln, srn;

	if (n <= 0)
		return;

	int* sl = new int[n];
	int* sr = new int[n];

	sln = -1;
	srn = -1;
	sl[++sln] = 0;
	sr[++srn] = n - 1;

	while(sln != - 1)
	{
		int l = sl[sln];
		int r = sr[srn];

		--sln;
		--srn;

		int i = l;
		int j = r;

		double mid = a[(l + r) / 2];
		do
		{
			while(a[i] > mid)
				++i;

			while(a[j] < mid)
				--j;

			if (i <= j)
			{
				swap(a[i], a[j]);
				swap(x[i], x[j]);
				swap(y[i], y[j]);

				++i;
				--j;
			}
		}while(i < j);

		if (i < r)
		{
			++sln;
			++srn;
			sl[sln] = i;
			sr[srn] = r;
		}

		if (j > l)
		{
			++sln;
			++srn;
			sl[sln] = l;
			sr[srn] = j;
		}
	}

	delete []sl;
	delete []sr;
}

IplImage* ScaleImage(IplImage* img, int percent)
{
	IplImage* des = cvCreateImage(cvSize((int)((float)img->width * (float)percent / 100.0), (int) ((float) img->height * (float)percent / 100.0)), 
			img->depth, img->nChannels);
	cvResize(img, des);
	return des;
}

IplImage* Rotate( IplImage* image, float angle) 
{
	CvScalar col = cvScalarAll(0);
	/* Compute the new size */
	int hypotenuse = (int)floor(sqrt((double)image->width*image->width + image->height*image->height));
	CvSize size = cvSize(hypotenuse, hypotenuse);

	/* Prepare some variables for rotation */
	CvRect rect = cvRect((hypotenuse-image->width)/2, (hypotenuse-image->height)/2, image->width, image->height);
	IplImage *rotatedImg = cvCreateImage(size, IPL_DEPTH_8U,image->nChannels);
	cvSet(rotatedImg, col);

	cvSetImageROI(rotatedImg, rect);
	cvCopy(image, rotatedImg);
	cvResetImageROI(rotatedImg);

	CvPoint2D32f center;
	center.x = hypotenuse/2;center.y = hypotenuse/2;
	CvMat *mapMatrix = cvCreateMat( 2, 3, CV_32FC1 );

	/* Rotate the image */
	IplImage *result = cvCloneImage(rotatedImg);
	cv2DRotationMatrix(center, angle, 1.0, mapMatrix);
	cvWarpAffine(rotatedImg, result, mapMatrix, CV_INTER_LINEAR + CV_WARP_FILL_OUTLIERS, col);

	/* release memory */
	cvReleaseMat(&mapMatrix);

	return result;
}

void CollectObjectPoint(IplImage* img, int X[], int Y[], int& size)		
{
	size = 0;

	uchar* dataI = (uchar*) img->imageData;
	int stepI = img->widthStep / sizeof(uchar);
	
	for(int i = 0; i < img->height; i++)
		for(int j = 0; j < img->width; j++)
			if (dataI[i * stepI + j] == 255)
			{
				X[size] = i;
				Y[size++] = j;
			}
}

CvMat* IntegralMatrix(IplImage* img)
{
	CvMat* sum = cvCreateMat(img->height + 1, img->width + 1, CV_MAKETYPE(CV_32S, img->nChannels));
	cvIntegral(img, sum, NULL, NULL);

	return sum;
}

inline int SumAt(CvMat* sum, int x1, int x2, int y1, int y2)
{
	int* data = (int*)sum->data.ptr;
	int step = sum->cols;
	++x2; ++y2;

	int s1 = data[x2 * step + y2];		// sum(x2, y2);
	int s2 = data[x1 * step + y2];		// sum(x1, y2);
	int s3 = data[x2 * step + y1];		// sum(x2, y1);
	int s4 = data[x1 * step + y1];		// sum(x1, y1);

	return s1 - s2 - s3 + s4;
}

bool nextBlock(int& x1, int& y1, int& x2, int& y2, int sizex, int sizey, int height, int width)
{
	x2 = x1 + sizex - 1;
	y2 = y1 + sizey - 1;

	if (y2 >= width)
	{
		x1 += sizex;
		y1 = 0;

		x2 = x1 + sizex - 1;
		y2 = y1 + sizey - 1;
	}

	return (x2 < height);
}

bool IsAccepted(IplImage* img, int x1, int x2, int y1, int y2, int thres)
{
	int dem = 0;
	uchar* dataI = (uchar*) img->imageData;
	int stepI = img->widthStep / sizeof(uchar);
	
	for(int i = x1; i <= x2; i++)
		for(int j = y1; j <= y2; j++)
			if (dataI[i * stepI + j] == 255)
				dem++;

	return dem >= thres;
}

Blog* MakeBlog(IplImage* tpl, IplImage* img, int sizex, int sizey, int threshold, int& sizeBlog)
{
	CvMat* sum = IntegralMatrix(tpl);

	Blog* bg = new Blog[maxBlog];
	sizeBlog = 0;

	int H = img->height;
	int W = img->width;

	int x1, y1, x2, y2;
	x1 = 0;
	y1 = 0;
	
	while(nextBlock(x1, y1, x2, y2, sizex, sizey, H, W))
	{
		if (IsAccepted(img, x1, x2, y1, y2, threshold))
		{
			bg[sizeBlog].topLeft_x = x1;
			bg[sizeBlog].topLeft_y = y1;
			bg[sizeBlog].botRight_x = x2;
			bg[sizeBlog].botRight_y = y2;
			bg[sizeBlog++].sum = SumAt(sum, x1, x2, y1, y2);
		}
		y1 += sizey;
	}
	
	cvReleaseMat(&sum);
	return bg;
}

double Diff(CvMat* sumImage, Blog* bg, int sizeBlog, int size, int x, int y)
{
	int x1, y1, x2, y2;
	int X1, Y1, X2, Y2;

	double diff = 0;
	double sum1 = 0;
	double sum2 = 0;
	
	double tu = 0;
	double mau1 = 0;
	double mau2 = 0;
	
	for(int i = 0; i < sizeBlog; i++)
	{
		x1 = bg[i].topLeft_x;
		y1 = bg[i].topLeft_y;

		x2 = bg[i].botRight_x;
		y2 = bg[i].botRight_y;
		
		X1 = x1 + x;
		Y1 = y1 + y;

		X2 = x2 + x;
		Y2 = y2 + y;

		int a = bg[i].sum;
		int b = SumAt(sumImage, X1, X2, Y1, Y2);
		
		sum1 += a;
		sum2 += b;

		tu += a * b;
		mau1 += a * a;
		mau2 += b * b;
	}
	
	if (sum2 >= 0.75 * sum1 && sum2 <= 1.25 * sum1 && mau1 != 0 && mau2 !=0)
		return tu / (sqrt(mau1 * mau2));
	else
		return limit;
}

double Corr(IplImage* I, IplImage* T, int X[], int Y[], int x, int y, int size) // Giá trị correlation 
{
	double tu = 0;
	double mau1 = 0;
	double mau2 = 0;
	
	uchar* dataT = (uchar*) T->imageData;
	int widthStept1 = T->widthStep / sizeof(uchar);
	
	uchar* dataI = (uchar*) I->imageData;
	int widthStepI = I->widthStep / sizeof(uchar);
	
	for(int t = 0; t < size; t++)
	{
		int i = X[t];
		int j = Y[t];

		int xNew = x + i;
		int yNew = y + j;
		
		double a = (double) dataT[i * widthStept1 + j]; 
		double b = (double) dataI[xNew * widthStepI + yNew];

		tu += a * b;
		mau1 += a * a;
		mau2 += b * b;
	}	

	if (mau1 != 0 && mau2 !=0 )
		return tu / (sqrt(mau1 * mau2));
	else
		return -1;
}

void Init(IplImage* T, IplImage* M, int sizex, int sizey, int threshold, bool rotation = false)
{
	if (rotation == false)
	{
		#pragma omp parallel for num_threads(cvGetNumThreads()) schedule(dynamic)
		{
			for(int i = nTempl; i < nScale + nTempl; i++)
			{
				templ[i] = ScaleImage(T, scaleFactor[i - nTempl]);
				pyrTempl[i] = ScaleImage(templ[i], 50);

				mask[i] = ScaleImage(M, scaleFactor[i - nTempl]);
				pyrMask[i] = ScaleImage(mask[i], 50);
				
				bg[i] = MakeBlog(pyrTempl[i], pyrMask[i], sizex, sizey, threshold, nBlog[i]);

				CollectObjectPoint(mask[i], pt_X[i], pt_Y[i], nPoint[i]);
				CollectObjectPoint(pyrMask[i], pt_pyr_X[i], pt_pyr_Y[i], nPoint_pyr[i]);
			}
		}
		nTempl += nScale;
	}
	else
	{
		for(int j = 0; j < 360; j += disOrient)
		{
			IplImage* c_T = Rotate(T, j);
			IplImage* c_M = Rotate(M, j);
			
			#pragma omp parallel for num_threads(cvGetNumThreads()) schedule(dynamic)
			{
				for(int i = nTempl; i < nScale + nTempl; i++)
				{
					templ[i] = ScaleImage(c_T, scaleFactor[i - nTempl]);
					pyrTempl[i] = ScaleImage(templ[i], 50);

					mask[i] = ScaleImage(c_M, scaleFactor[i - nTempl]);
					pyrMask[i] = ScaleImage(mask[i], 50);
					
					bg[i] = MakeBlog(pyrTempl[i], pyrMask[i], sizex, sizey, threshold, nBlog[i]);

					CollectObjectPoint(mask[i], pt_X[i], pt_Y[i], nPoint[i]);
					CollectObjectPoint(pyrMask[i], pt_pyr_X[i], pt_pyr_Y[i], nPoint_pyr[i]);
				}
			}

			nTempl += nScale;
			cvReleaseImage(&c_T);
			cvReleaseImage(&c_M);
		}
	}
}

CvPoint Search(IplImage* img, IplImage* img_pyr, int index, float& val)
{
	CvMat* sumImage = IntegralMatrix(img_pyr);

	IplImage* c_pyrTempl= pyrTempl[index];
	IplImage* c_templ = templ[index];

	int Hi = img_pyr->height;
	int Wi = img_pyr->width;

	int Ht = c_pyrTempl->height;
	int Wt = c_pyrTempl->width;

	Blog* c_bg = bg[index];
	
	int c_nPoint_pyr = nPoint_pyr[index];
	int c_nPoint = nPoint[index];
	int c_nBlog = nBlog[index];

	int size = abs(c_bg[0].botRight_x - c_bg[0].topLeft_x + 1) * abs(c_bg[0].topLeft_y - c_bg[0].botRight_y + 1);
	
	int* pt_x = pt_X[index];
	int* pt_y = pt_Y[index];

	int* x = new int[(Hi - Ht + 1) * (Wi - Wt + 1)];
	int* y = new int[(Hi - Ht + 1) * (Wi - Wt + 1)];
	double* cor = new double[(Hi - Ht + 1) * (Wi - Wt + 1)];
	
	double max = 0;
	int count = 0;

	for(int i = 0; i < Hi - Ht + 1; i++)
		for(int j = 0; j < Wi - Wt + 1; j++)
		{
			double diff = Diff(sumImage, c_bg, c_nBlog, size, i, j);
			if (diff != limit && diff >= thresholdMatch)
			{
				x[count] = i * 2;
				y[count] = j * 2;
				cor[count++] = diff;
			}
		}

	QuickSortStack(cor, x, y, count);
	
	int x_re = -1;
	int y_re = -1;
	int n = min(30, count);

	for(int i = 0; i < n; i++)
	{
		double r = Corr(img, c_templ, pt_x, pt_y, x[i], y[i], c_nPoint);

		if (r >= max)
		{
			max = r;
			x_re = x[i];
			y_re = y[i];
		}
	}

	val = max;
	cvReleaseMat(&sumImage);
	return cvPoint(y_re, x_re);
}
//----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------
#define nTest 540
#define accept 15

char fileNameOfTest[100] = "TestImg_";
char typNameOfTest[100] = ".jpg";
char fileNameOfMask[100] = "MDoremon";
char typNameOfMask[100] = ".png";
char fileNameOfTemplate[100] = "Doremon";
char typeOfTemplate[100] = ".png";
char resultPath[100] = "D:\\CombineImg\\Final_Result\\TestImg_";
char choicedTemplate[100] = "D:\\CombineImg\\Final_Result\\TemplateImg_";
char typeOfIndexFile[100] = ".txt";

void getFileName(int index, char name[100], char type[100], char s[100])
{
	char num[100];
	itoa(index, num, 10);
	int count = 0;
	for(int i = 0; i < strlen(name); i++)
		s[count++] = name[i];
	for(int i = 0; i < strlen(num); i++)
		s[count++] = num[i];
	for(int i = 0; i < strlen(type); i++)
		s[count++] = type[i];
	s[count] = '\0';
}

void getFullTest(int index, char test[100], char temp[100], char result[100], char choiced[100], char fileindex[100], char filemask[100])
{
	getFileName(index, fileNameOfTest, typNameOfTest, test);
	getFileName(index, resultPath, typNameOfTest, result);
	getFileName(index, choicedTemplate, typNameOfTest, choiced);
	getFileName(index, fileNameOfTest, typeOfIndexFile, fileindex);
	getFileName(index / 8 + 1, fileNameOfTemplate, typeOfTemplate, temp);
	getFileName(index / 8 + 1, fileNameOfMask, typNameOfMask, filemask);
}

bool testIndex(int x, int y, int a, int b)
{
	return x <= a + accept && x >= a - accept && y >= b - accept && y <= b + accept;
}

void Highlight(IplImage* img, IplImage* templ, CvPoint pt)
{
	cvRectangle(img, pt, cvPoint(pt.x + templ->width, pt.y + templ->height), cvScalar(0xff,0x00,0x00), 2);
}
//----------------------------------------------------------------------------------------------------------
int main()
{
	char test[100];
	char temp[100];
	char result[100];
	char choiced[100];
	char fileindex[100];
	char filemask[100];
	DynamicAllocation();
	int method = 3;
	time_t t;		// Time value
	long t_start;	// Start counting time
	long t_end;		// End counting time

	int sizex = 5;
	int sizey = 5;
	int threshold = 20;


	for(int i = 1; i <= 7; i++)
	{
		vector<CvPoint> highestPoints;	
		highestPoints.clear();
		getFullTest(i, test, temp, result, choiced, fileindex, filemask);

		IplImage* img = cvLoadImage(test, 0);
		IplImage* img1 = cvLoadImage(temp, 0);
		IplImage* maskx = cvLoadImage(filemask, 0);
		IplImage* col = cvLoadImage(test, 1);
		Init(img1, maskx, sizex, sizey, threshold, false);
		ifstream myfile (fileindex);

		int x, y;
		myfile >> x;
		myfile >> y;

		IplImage* img_pyr = ScaleImage(img, 50);

		t_start = clock();
		float best_val = -100000000;
		CvPoint good_point = cvPoint(0, 0);
		int index = 0;
		for(int j = 0; j < nTempl; j++)
		{
			float val; 
			CvPoint pt = Search(img, img_pyr, j, val);
			if (val > best_val)
			{
				best_val = val;
				good_point = pt;
				index = j;
			}
		}

		cout << "Test " << i << ": " << clock() - t_start << "ms" << endl;
		Highlight(col, templ[index], good_point);
		
		cvNamedWindow("Result");
		cvShowImage("Result", col);
		cvWaitKey(0);
		
		Reset();
		cvReleaseImage(&col);
		cvReleaseImage(&img);
		cvReleaseImage(&img1);
		cvReleaseImage(&maskx);
		cvReleaseImage(&img_pyr);
	}
	Release();
}