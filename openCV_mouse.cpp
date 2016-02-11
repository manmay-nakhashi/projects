int trackObject = 0;
Point origin,center;
Rect selection;
int vmin = 10, vmax = 256, smin = 30;
void drive(Point);

static void intro()
{
	system("clear");
	printf("------------------------------\n");
	printf("CDAC Project (2015)\n");
	printf("Gesture based mouse\n");
	printf("------------------------------\n");
	printf("Team Members : \n");
	printf("------------------------------\n");
	printf("Akash Sinha\n");
	printf("Manmay Nakhashi\n");
	printf("Ravi Pulist\n");
	printf("------------------------------\n");
}
static void onMouse( int event, int x, int y, int, void* )
{
	if( selectObject )
	{
		selection.x = MIN(x, origin.x);
		selection.y = MIN(y, origin.y);
		selection.width = std::abs(x - origin.x);
		selection.height = std::abs(y - origin.y);
		selection &= Rect(0, 0, image.cols, image.rows);
	}
	switch( event )
	{
		case CV_EVENT_LBUTTONDOWN:
		origin = Point(x,y);
		selection = Rect(x,y,0,0);
		selectObject = true;
		break;
		case CV_EVENT_LBUTTONUP:
		selectObject = false;
		if( selection.width > 0 && selection.height > 0 )
		trackObject = -1;
		break;
	}
}
static void help()
{
	printf("------------------------------\n");
	printf("Select the colour to be detected\n");
	printf("with mouse on the video stream \n");
	printf("shown in the screen.\n");
	printf("------------------------------\n");
}


int main( int argc, const char** argv )
{
	intro();
	help();
	VideoCapture cap;
	Rect trackWindow;
	int hsize = 16;
	float hranges[] = {0,180};
	const float* phranges = hranges;
	int camNum = 0;
	if(argc == 2)
	camNum = atoi(argv[1]);
	cap.open(camNum);
	printf("%f\t%f\n",cap.get(3),cap.get(4));
	if( !cap.isOpened() )
	{
		help();
		cout << "***Could not initialize capturing...***\n";
		cout << "Current parameter's value: " << camNum << endl;
		return -1;
	}
	namedWindow( "CamShift Object Tracker", 0 );
	setMouseCallback( "CamShift Object Tracker", onMouse, 0 );
	Mat frame, hsv, hue, mask, hist, histimg = Mat::zeros(200, 320, CV_8UC3),backproj;
	bool paused = false;
	for(;;)
	{
		if( !paused )
		{
			cap >> frame;
			if( frame.empty() )
			break;
		}
		frame.copyTo(image);
		if( !paused )
		{
			cvtColor(image, hsv, CV_BGR2HSV);
			if( trackObject )
			{
				int _vmin = vmin, _vmax = vmax;
				inRange(hsv, Scalar(0, smin, MIN(_vmin,_vmax)),
				Scalar(180, 256, MAX(_vmin, _vmax)), mask);
				int ch[] = {0, 0};
				hue.create(hsv.size(), hsv.depth());
				mixChannels(&hsv, 1, &hue, 1, ch, 1);
				if( trackObject < 0 )
				{
					Mat roi(hue, selection), maskroi(mask, selection);
					calcHist(&roi, 1, 0, maskroi, hist, 1, &hsize, &phranges);
					normalize(hist, hist, 0, 255, CV_MINMAX);
					trackWindow = selection;
					trackObject = 1;
				}
				calcBackProject(&hue, 1, 0, hist, backproj, &phranges);
				backproj &= mask;
				RotatedRect trackBox = CamShift(backproj, trackWindow,TermCriteria( CV_TERMCRIT_EPS |CV_TERMCRIT_ITER, 10, 1));
				//printf("%f\t%f\n",trackBox.size.width,trackBox.size.height);
				center=trackBox.center;
				ellipse( image, trackBox, Scalar(0,0,255), 3, CV_AA );
				drive(center);
			}
		}
		else if( trackObject < 0 )
		paused = false;
		if( selectObject && selection.width > 0 && selection.height > 0 )
		{
			Mat roi(image, selection);
			bitwise_not(roi, roi);
		}
		imshow( "CamShift Object Tracker", image );
		char c = (char)waitKey(10);
		//printf("%d\t%d\n",center.x,center.y);
		if( c == 27 )
		break;
		switch(c)
		{
			case 's':
			trackObject = 0;
			histimg = Scalar::all(0);
			break;
			case 'p':
			paused = !paused;
			break;
			default:
				;
		}
	}
	return 0;
}


void drive(Point ctrl)
{
	string x,y;
	stringstream sstr; stringstream sstr2;
	sstr<<3*ctrl.x;
	x = sstr.str();
    	sstr2<<3*ctrl.y;
    	y = sstr2.str();
	string command = "xdotool mousemove " + x + " " + y;

    	///Converting command string to a form that system() accepts.
    	const char *com = command.c_str();
    	system(com);
	//printf("\n ");
	printf("\r %d",ctrl.x);
	printf("\r %d",ctrl.y);
		
}
