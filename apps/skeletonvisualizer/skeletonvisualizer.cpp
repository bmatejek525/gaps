// Source file for skeleton visualizer



// include files

#include "R3Graphics/R3Graphics.h"
#include "RNDataStructures/RNDataStructures.h"
#include "fglut/fglut.h"
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>



// GLUT defines

#define ENTER 13
#define ESCAPE 27
#define SPACEBAR 32


// class declarations
struct RNMeta;
struct SWCEntry;
struct MergeCandidate;



// program arguments

static int print_debug = 0;
static int print_verbose = 0;
// maximum distance in nanometers
static const char* prefix = NULL;



// program variables

static RNScalar resolution[3] = { 6, 6, 30 };
static int grid_size[3] = { -1, -1, -1 };
static R3Affine transformation = R3null_affine;
static R3Viewer *viewer = NULL;
static R3Box world_box = R3null_box;




// voxel grids

static R3Grid *grid = NULL;
static R3Grid *skeleton_grid = NULL;
static int selected_slice_index = 0;
static int show_slice = 0;
static int projection_dim = RN_Z;



// GLUT variables

static int GLUTwindow = 0;
static int GLUTwindow_height = 1200;
static int GLUTwindow_width = 1200;
static int GLUTmouse[2] = { 0, 0 };
static int GLUTbutton[3] = { 0, 0, 0 };
static int GLUTmodifiers = 0;



// color arrays

static RNScalar background_color[3] = { 1, 1, 1 };


struct SkeletonPair {
    SkeletonPair(unsigned long point_one, unsigned long point_two) :
    point_one(point_one),
    point_two(point_two)
    {
    }

    unsigned long point_one;
    unsigned long point_two;
};


// mapping and random access variables

static std::map<unsigned long, unsigned long> label_to_index;
static unsigned long *index_to_label = NULL;
static std::vector<unsigned long> *segmentations = NULL;
static std::vector<SkeletonPair> *skeletons = NULL;


// display variables

static int show_bbox = 1;
static int show_skeleton = 1;
static int show_feature_box = 1;
static int show_segmentation = 1;
static unsigned int segmentation_index = 7;
static RNScalar downsample_rate = 1.5;




////////////////////////////////////////////////////////////////////////
// Input/output functions
////////////////////////////////////////////////////////////////////////

struct RNMeta {
    // instance variables
    int resolution[3];
    char prefix[4096];
    char gold_filename[4096];
    char gold_dataset[128];
    char image_filename[4096];
    char image_dataset[128];
    char mask_filename[4096];
    char mask_dataset[128];
    char rhoana_filename[4096];
    char rhoana_dataset[128];
    R3Box world_box;
    R3Box scaled_box;
};

// declare here
static RNMeta meta_data;



static int
ReadMetaData(const char *prefix)
{
    // update the meta data prefix
    strncpy(meta_data.prefix, prefix, 4096);

    // get the meta data filename
    char meta_data_filename[4096];
    sprintf(meta_data_filename, "meta_data/%s.meta", prefix);

    // open the file
    FILE *fp = fopen(meta_data_filename, "r");
    if (!fp) { fprintf(stderr, "Failed to read %s\n", meta_data_filename); return 0; }

    // dummy variable
    char comment[4096];

    // read in requisite information
    if (!fgets(comment, 4096, fp)) return 0;
    if (fscanf(fp, "%dx%dx%d\n", &(meta_data.resolution[RN_X]), &(meta_data.resolution[RN_Y]), &(meta_data.resolution[RN_Z])) != 3) return 0;
    
    // skip affinities
    if (!fgets(comment, 4096, fp)) return 0;
    if (!fgets(comment, 4096, fp)) return 0;

    // skip boundaries
    if (!fgets(comment, 4096, fp)) return 0;
    if (!fgets(comment, 4096, fp)) return 0;

    // skip gold
    if (!fgets(comment, 4096, fp)) return 0;
    if (!fgets(comment, 4096, fp)) return 0;

    // read image
    if (!fgets(comment, 4096, fp)) return 0;
    if (!fgets(comment, 4096, fp)) return 0;

    // skip mask
    if (!fgets(comment, 4096, fp)) return 0;
    if (!fgets(comment, 4096, fp)) return 0;

    // read segmentation
    if (!fgets(comment, 4096, fp)) return 0;
    if (fscanf(fp, "%s %s\n", meta_data.rhoana_filename, meta_data.rhoana_dataset) != 2) return 0;

    // skip synapse
    if (!fgets(comment, 4096, fp)) return 0;
    if (!fgets(comment, 4096, fp)) return 0;

    // update the global resolution
    for (int dim = 0; dim <= 2; ++dim)
        resolution[dim] = meta_data.resolution[dim];

    // skip world box
    if (!fgets(comment, 4096, fp)) return 0;
    if (!fgets(comment, 4096, fp)) return 0;
    meta_data.world_box = R3null_box;
    meta_data.scaled_box = R3null_box;

    // close the file
    fclose(fp);


    // return success
    return 1;
}


static int ReadData(void)
{
    // start statistics
    RNTime start_time;
    start_time.Read();

    R3Grid **grids = RNReadH5File(meta_data.rhoana_filename, meta_data.rhoana_dataset);
    grid = grids[0];
    delete[] grids;

    grid_size[RN_X] = grid->XResolution();
    grid_size[RN_Y] = grid->YResolution();
    grid_size[RN_Z] = grid->ZResolution();

    // TODO fix hard coding names
    char skeleton_filename[4096];
    sprintf(skeleton_filename, "skeletons-mine.h5", prefix);
    char skeleton_dataset[4096];
    sprintf(skeleton_dataset, "main");

    R3Grid **skeleton_grids = RNReadH5File(skeleton_filename, skeleton_dataset);
    skeleton_grid = skeleton_grids[0];
    delete[] skeleton_grids;

    // print statistics
    if(print_verbose) {
        printf("Read voxel grid...\n");
        printf("  Time = %.2f seconds\n", start_time.Elapsed());
        printf("  Grid Size = (%d %d %d)\n", grid->XResolution(), grid->YResolution(), grid->ZResolution());
        printf("  Resolution = (%0.2lf %0.2lf %0.2lf)\n", resolution[RN_X], resolution[RN_Y], resolution[RN_Z]);
    }

    // return success
    return 1;
}



////////////////////////////////////////////////////////////////////////
// Preprocessing functions
////////////////////////////////////////////////////////////////////////

static void 
IndexToIndices(int index, int& ix, int& iy, int& iz)
{
  // Set indices of grid value at index
  iz = index / (grid_size[RN_X] * grid_size[RN_Y]);
  iy = (index - iz * grid_size[RN_X] * grid_size[RN_Y]) / grid_size[RN_X];
  ix = index % grid_size[RN_X];
}



static long IndicesToIndex(long ix, long iy, long iz)
{
  return iz * grid_size[RN_X] * grid_size[RN_Y] + iy * grid_size[RN_X] + ix;
}



static void LabelToIndexMapping(void)
{
    long zres = grid->ZResolution();
    long yres = grid->YResolution();
    long xres = grid->XResolution();

    // find which labels are present
    unsigned long maximum_segmentation = (unsigned long)(grid->Maximum() + 0.5) + 1;
    RNBoolean *present_labels = new RNBoolean[maximum_segmentation];
    for (unsigned long iv = 0; iv < maximum_segmentation; ++iv)
        present_labels[iv] = FALSE;

    // iterate over the entire volume to find present labels
    for (long iv = 0; iv < grid->NEntries(); ++iv) {
        unsigned long label = (unsigned long)(grid->GridValue(iv) + 0.5);
        present_labels[label] = TRUE;
    }

    // create the mapping from segments labels to indices
    label_to_index = std::map<unsigned long, unsigned long>();
    unsigned long nunique_labels = 1; /* 1 indexed for this to work */
    for (unsigned long iv = 1; iv < maximum_segmentation; ++iv) {
        if (present_labels[iv] && !label_to_index[iv]) {
            label_to_index[iv] = nunique_labels;
            nunique_labels++;
        }
    }

    // create the mapping from indices to labels
    index_to_label = new unsigned long[nunique_labels];
    nunique_labels = 1;
    for (unsigned long iv = 1; iv < maximum_segmentation; ++iv) {
        if (present_labels[iv]) {
            index_to_label[nunique_labels] = iv;
            nunique_labels++;
        }
    }

    // free memory
    delete[] present_labels;

    // allocate memory for the segmentation vectors
    segmentations = new std::vector<unsigned long>[nunique_labels];
    skeletons = new std::vector<SkeletonPair>[nunique_labels];
    for (unsigned long iv = 0; iv < nunique_labels; ++iv) {
        segmentations[iv] = std::vector<unsigned long>();
        skeletons[iv] = std::vector<SkeletonPair>();
    }


    // iterate over the entire volume
    int iv = 0;
    for (int iz = 0; iz < zres; ++iz) {
        for (int iy = 0; iy < yres; ++iy) {
            for (int ix = 0; ix < xres; ++ix, ++iv) {
                unsigned long label = (unsigned long)(grid->GridValue(ix, iy, iz) + 0.5);
                if (!label) continue;

                // is this pixel boundary
                RNBoolean boundary = FALSE;
                if (ix > 0 && label != (unsigned long)(grid->GridValue(ix - 1, iy, iz) + 0.5)) boundary = TRUE;
                if (iy > 0 && label != (unsigned long)(grid->GridValue(ix, iy - 1, iz) + 0.5)) boundary = TRUE;
                if (iz > 0 && label != (unsigned long)(grid->GridValue(ix, iy, iz - 1) + 0.5)) boundary = TRUE;
                if (ix < grid_size[RN_X] - 1 && label != (unsigned long)(grid->GridValue(ix + 1, iy, iz) + 0.5)) boundary = TRUE;
                if (iy < grid_size[RN_Y] - 1 && label != (unsigned long)(grid->GridValue(ix, iy + 1, iz) + 0.5)) boundary = TRUE;
                if (iz < grid_size[RN_Z] - 1 && label != (unsigned long)(grid->GridValue(ix, iy, iz + 1) + 0.5)) boundary = TRUE;

                // get this index
                unsigned long index = label_to_index[label];
                rn_assertion((0 < index) && (index < nunique_labels));

                // add to the vector
                if (boundary) segmentations[index].push_back(iv);

                // does this belong to the skeleton
                unsigned long skeleton_value = (unsigned long) (skeleton_grid->GridValue(iv) + 0.5);
                if (!skeleton_value) continue;

                // find all skeletons in immediate neighborhood
                int nneighbors = 0;
/*                for (int ik = iz - 1; ik <= iz + 1; ++ik) {
                    if (ik < 0 or ik >= zres) continue;
                    for (int ij = iy - 1; ij <= iy + 1; ++ij) {
                        if (ij < 0 or ij >= yres) continue;
                        for (int ii = ix - 1; ii <= ix + 1; ++ii) {
                            if (ii < 0 or ii >= xres) continue;
                            if (ik == iz and ij == iy and ii == ix) continue;
                            // get this index
                            unsigned long neighbor_index = IndicesToIndex(ii, ij, ik);
                            unsigned long neighbor_skeleton = (unsigned long) (skeleton_grid->GridValue(neighbor_index) + 0.5);
                            if (neighbor_skeleton != skeleton_value) continue;
                            skeletons[index].push_back(SkeletonPair(iv, neighbor_index));
                            nneighbors ++;
                        }
                    }
                }*/
                if (nneighbors == 0) 
                    skeletons[index].push_back(SkeletonPair(iv, iv));
            }
        }
    }
}



////////////////////////////////////////////////////////////////////////
// Drawing utility functions
////////////////////////////////////////////////////////////////////////

static RNRgb
Color(unsigned long value)
{
    RNScalar red = (RNScalar) (((107 * value) % 700) % 255) / 255.0;
    RNScalar green = (RNScalar) (((509 * value) % 900) % 255) / 255.0;
    RNScalar blue = (RNScalar) (((200 * value) % 777) % 255) / 255.0;

    return RNRgb(red, green, blue);
}



static void DrawSkeleton(int index)
{
    transformation.Pop();

    RNLoadRgb(RNblack_rgb);
    glPointSize(4);
    glBegin(GL_POINTS);
    for (unsigned int iv = 0; iv < skeletons[index].size(); ++iv) {
        int ix, iy, iz;
        IndexToIndices(skeletons[index][iv].point_one, ix, iy, iz);
        glVertex3f(resolution[RN_X] * ix, resolution[RN_Y] * iy, resolution[RN_Z] * iz);

        //R3Line(R3Point(resolution[RN_X] * ix, resolution[RN_Y] * iy, resolution[RN_Z] * iz), R3Point(resolution[RN_X] * ii, resolution[RN_Y] * ij, resolution[RN_Z] * ik)).Draw();
    }
    glEnd();
    glPointSize(1);

    transformation.Push();
}



static void DrawIndividualSegment(int index)
{
    glBegin(GL_POINTS);
    for (unsigned int iv = 0; iv < segmentations[index].size(); ++iv) {
        if (RNRandomScalar() > 1.0 / downsample_rate) continue;
        int ix, iy, iz;
        IndexToIndices(segmentations[index][iv], ix, iy, iz);
        glVertex3f(ix, iy, iz);
    }
    glEnd();
    if (show_skeleton) DrawSkeleton(index);
}



static void DrawSegmentations(void)
{
    // push the transformation
    transformation.Push();


    RNLoadRgb(Color(segmentation_index));
    DrawIndividualSegment(segmentation_index);

    
    // draw the slice if desired
    RNLoadRgb(RNwhite_rgb);
    if (show_slice) grid->DrawColorSlice(projection_dim, selected_slice_index);

    // pop the transformation
    transformation.Pop();
}



////////////////////////////////////////////////////////////////////////
// GLUT interface functions
////////////////////////////////////////////////////////////////////////

void GLUTStop(void)
{
    // destroy window
    glutDestroyWindow(GLUTwindow);

    // delete the neuron data
    RNTime start_time;
    start_time.Read();

    if (grid) delete grid;

    // print statistics
    if(print_verbose) {
        printf("Deleted data...\n");
        printf("  Time = %.2f seconds\n", start_time.Elapsed());
        fflush(stdout);
    }

    // exit
    exit(0);
}



void GLUTRedraw(void)
{
    // clear window
    glClearColor(background_color[0], background_color[1], background_color[2], 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // drawing varies on projection
    viewer->Camera().Load();

    // set lights
    static GLfloat light0_position[] = { 3.0, 4.0, 5.0, 0.0 };
    glLightfv(GL_LIGHT0, GL_POSITION, light0_position);
    static GLfloat light1_position[] = { -3.0, -2.0, -3.0, 0.0 };
    glLightfv(GL_LIGHT1, GL_POSITION, light1_position);

    // prologue
    glDisable(GL_LIGHTING);

    // draw neuron data bounding box
    if(show_bbox) {
        RNLoadRgb(RNblack_rgb);
        world_box.Outline();
    }

    // draw machine labels and skeletons
    DrawSegmentations();

    // epilogue
    glEnable(GL_LIGHTING);

    // write the title
    char title[4096];
    sprintf(title, "Skeleton Visualizer - %lu\n", index_to_label[segmentation_index]);

    glutSetWindowTitle(title);


    // swap buffers
    glutSwapBuffers();
}



void GLUTResize(int w, int h)
{
    // resize window
    glViewport(0, 0, w, h);

    // resize viewer viewport
    viewer->ResizeViewport(0, 0, w, h);

    // remember window size
    GLUTwindow_width = w;
    GLUTwindow_height = h;

    // redraw
    glutPostRedisplay();
}



void GLUTMotion(int x, int y)
{
    // invert y coordinate
    y = GLUTwindow_height - y;

    // compute mouse movement
    int dx = x - GLUTmouse[0];
    int dy = y - GLUTmouse[1];

    // world in hand navigation
    R3Point origin = world_box.Centroid();
    if(GLUTbutton[0])
        viewer->RotateWorld(1.0, origin, x, y, dx, dy);
    else if(GLUTbutton[1])
        viewer->ScaleWorld(1.0, origin, x, y, dx, dy);
    else if(GLUTbutton[2])
        viewer->TranslateWorld(1.0, origin, x, y, dx, dy);
    
    // redisplay if a mouse was down
    if(GLUTbutton[0] || GLUTbutton[1] || GLUTbutton[2]) glutPostRedisplay();

    // remember mouse position
    GLUTmouse[0] = x;
    GLUTmouse[1] = y;
}



void GLUTMouse(int button, int state, int x, int y)
{
    // invert y coordinate
    y = GLUTwindow_height - y;

    // remember mouse position
    GLUTmouse[0] = x;
    GLUTmouse[1] = y;

    // remember button state
    int b = (button == GLUT_LEFT_BUTTON) ? 0 : ((button == GLUT_MIDDLE_BUTTON) ? 1 : 2);
    GLUTbutton[b] = (state == GLUT_DOWN) ? 1 : 0;

    // remember modifiers
    GLUTmodifiers = glutGetModifiers();

    // remember mouse position
    GLUTmouse[0] = x;
    GLUTmouse[1] = y;

    // redraw
    glutPostRedisplay();
}



void GLUTSpecial(int key, int x, int y)
{
    // invert y coordinate
    y = GLUTwindow_height - y;

    // remember mouse position
    GLUTmouse[0] = x;
    GLUTmouse[1] = y;

    // remember modifiers
    GLUTmodifiers = glutGetModifiers();

    switch(key) {
        case GLUT_KEY_UP: {
            selected_slice_index += 1;
            if (selected_slice_index >= grid_size[projection_dim])
                selected_slice_index = grid_size[projection_dim] - 1;
            break;
        }

        case GLUT_KEY_DOWN: {
            selected_slice_index -= 1;
            if (selected_slice_index < 0)
                selected_slice_index = 0;
            break;
        }

        case GLUT_KEY_LEFT: {
            segmentation_index--;
            if(segmentation_index < 1) segmentation_index = 1;
            break;
        }

        case GLUT_KEY_RIGHT: {
            segmentation_index++;
            if(segmentation_index >= label_to_index.size())
                segmentation_index = label_to_index.size() - 1;
            break;
        }
    }

    // redraw
    glutPostRedisplay();
}



void GLUTKeyboard(unsigned char key, int x, int y)
{
    // invert y coordinate
    y = GLUTwindow_height - y;

    // remember mouse position
    GLUTmouse[0] = x;
    GLUTmouse[1] = y;

    // remember modifiers
    GLUTmodifiers = glutGetModifiers();

    // keys regardless of projection status
    switch(key) {
        case 'B':
        case 'b': {
            show_bbox = 1 - show_bbox;
            break;
        }

        case 'D':
        case 'd': {
            show_segmentation = 1 - show_segmentation;
            break;
        }

        case 'F':
        case 'f': {
            show_feature_box = 1 - show_feature_box;
            break;
        }

        case 'S':
        case 's': {
            show_skeleton = 1 - show_skeleton;
            break;
        }

        case 'W':
        case 'w': {
            show_slice = 1 - show_slice;
            break;
        }

        case 'X':
        case 'x': {
            projection_dim = RN_X;
            break;
        }

        case 'Y':
        case 'y': {
            projection_dim = RN_Y;
            break;
        }

        case 'Z':
        case 'z': {
            projection_dim = RN_Z;
            break;
        }

        case ESCAPE: {
            GLUTStop();
            break;
        }
    }

    // redraw
    glutPostRedisplay();
}



void GLUTInit(int* argc, char** argv)
{
    // open window
    glutInit(argc, argv);

    glutInitWindowPosition(10, 10);
    glutInitWindowSize(GLUTwindow_width, GLUTwindow_height);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    GLUTwindow = glutCreateWindow("Skeleton Visualizer");
    // initialize background color
    glClearColor(background_color[0], background_color[1], background_color[2], 1.0);
    // initialize lights
    static GLfloat lmodel_ambient[] = { 0.2, 0.2, 0.2, 1.0 };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lmodel_ambient);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
    static GLfloat light0_diffuse[] = { 1.0, 1.0, 1.0, 1.0 };
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light0_diffuse);
    glEnable(GL_LIGHT0);
    static GLfloat light1_diffuse[] = { 0.5, 0.5, 0.5, 1.0 };
    glLightfv(GL_LIGHT1, GL_DIFFUSE, light1_diffuse);
    glEnable(GL_LIGHT1);
    glEnable(GL_NORMALIZE);
    glEnable(GL_LIGHTING);
    // initialize graphics mode
    glEnable(GL_DEPTH_TEST);
    // initialize GLUT callback functions
    glutDisplayFunc(GLUTRedraw);
    glutReshapeFunc(GLUTResize);
    glutKeyboardFunc(GLUTKeyboard);
    glutSpecialFunc(GLUTSpecial);
    glutMouseFunc(GLUTMouse);
    glutMotionFunc(GLUTMotion);
}



void GLUTMainLoop(void)
{
    // run main loop -- never returns
    glutMainLoop();
}



////////////////////////////////////////////////////////////////////////
// Viewer functions
////////////////////////////////////////////////////////////////////////

static R3Viewer* CreateViewer(void)
{
    // start statistics
    RNTime start_time;
    start_time.Read();

    if (world_box.IsEmpty()) RNAbort("Error in CreateViewer - box is empty");
    RNLength radius = world_box.DiagonalRadius();
    if (radius < 0 || RNIsInfinite(radius)) RNAbort("Error in CreateViewer - radius must be positive finite");

    // set up camera view looking down the z axis
    static R3Vector initial_camera_towards = R3Vector(0.0, 0.0, -1.5);
    static R3Vector initial_camera_up = R3Vector(0.0, 1.0, 0.0);
    R3Point initial_camera_origin = world_box.Centroid() - initial_camera_towards * 2.5 * radius;
    R3Camera camera(initial_camera_origin, initial_camera_towards, initial_camera_up, 0.4, 0.4, 0.1 * radius, 1000.0 * radius);
    R2Viewport viewport(0, 0, GLUTwindow_width, GLUTwindow_height);
    R3Viewer *viewer = new R3Viewer(camera, viewport);

    // print statistics
    if(print_verbose) {
        printf("Created viewer ...\n");
        printf("  Time = %.2f seconds\n", start_time.Elapsed());
        printf("  Origin = %g %g %g\n", camera.Origin().X(), camera.Origin().Y(), camera.Origin().Z());
        printf("  Towards = %g %g %g\n", camera.Towards().X(), camera.Towards().Y(), camera.Towards().Z());
        printf("  Up = %g %g %g\n", camera.Up().X(), camera.Up().Y(), camera.Up().Z());
        printf("  Fov = %g %g\n", camera.XFOV(), camera.YFOV());
        printf("  Near = %g\n", camera.Near());
        printf("  Far = %g\n", camera.Far());
        fflush(stdout);
    }

    // return viewer
    return viewer;
}



////////////////////////////////////////////////////////////////////////
// Program argument parsing
////////////////////////////////////////////////////////////////////////

static int ParseArgs(int argc, char** argv)
{
    // parse arguments
    argc--; argv++;
    while(argc > 0) {
        if((*argv)[0] == '-') {
            if(!strcmp(*argv, "-v")) print_verbose = 1;
            else if(!strcmp(*argv, "-debug")) print_debug = 1;
            else { fprintf(stderr, "Invalid program argument: %s\n", *argv); return 0; }
        } else {
            if(!prefix) prefix = *argv;
            else { fprintf(stderr, "Invalid program argument: %s\n", *argv); return 0; }
        }
        argv++; argc--;
    }

    // error if there is no input name
    if(!prefix) {
        fprintf(stderr, "Need to supply a prefix for data files\n");
        return 0;
    }

    // return success
    return 1;
}



////////////////////////////////////////////////////////////////////////
// Main program
////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
    // parse program arguments
    if(!ParseArgs(argc, argv)) exit(-1);

    /////////////////////////////////
    //// Read in the voxel files ////
    /////////////////////////////////

    if (!ReadMetaData(prefix)) exit(-1);
    if (!ReadData()) exit(-1);

    // map the labels to indices
    LabelToIndexMapping();

    // set world box
    world_box = R3Box(0, 0, 0, resolution[RN_X] * grid_size[RN_X], resolution[RN_Y] * grid_size[RN_Y], resolution[RN_Z] * grid_size[RN_Z]);
    // get the transformation
    transformation = R3Affine(R4Matrix(resolution[RN_X], 0, 0, 0, 0, resolution[RN_Y], 0, 0, 0, 0, resolution[RN_Z], 0, 0, 0, 0, 1));
    // create viewer
    viewer = CreateViewer();
    if (!viewer) exit(-1);
    // initialize GLUT
    GLUTInit(&argc, argv);
    // run GLUT interface
    GLUTMainLoop();

    // return success
    return 1;
}