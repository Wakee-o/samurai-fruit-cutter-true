#include "Render.h"
#include "GUItextRectangle.h"
#include "Texture.h"

#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

// Внутренняя логика "движка"
#include "MyOGL.h"
extern OpenGL gl;

#include "Light.h"
Light light;

#include "Camera.h"
Camera camera;

// Глобальные переключатели лабораторной работы
bool texturing = true;
bool lightning = true;
bool alpha = true;

// Текстовая панель HUD
GuiTextRectangle text;

// Простая текстура из исходного проекта: "квадратик станкина", натянутый на грань пола
Texture stankin_tex;

float view_matrix[16];
double full_time = 0.0;

const double PI = 3.14159265358979323846;
const double SLASH_DURATION = 0.42;
const double GRAVITY = 2.85;

enum SamuraiPose
{
    POSE_IDLE = 0,
    POSE_SLASH_LEFT,
    POSE_SLASH_RIGHT,
    POSE_SLASH_UP
};

struct Fruit
{
    double x, y, z;
    double vx, vy;
    double baseZ;
    double phase;
    double radius;
    int type;
};

std::vector<Fruit> fruits;
SamuraiPose samuraiPose = POSE_IDLE;
double slashTimer = 0.0;
int score = 0;
int missed = 0;
bool paused = false;

// Фиксированная позиция источника света для этой сцены.
// Переключатель L включает и выключает освещение, но источник света больше не перемещается.
const float sceneLightX = -2.8f;
const float sceneLightY = 2.4f;
const float sceneLightZ = 1.7f;

// ------------------------------------------------------------
// Служебные функции
// ------------------------------------------------------------

static double rnd01(int seed)
{
    int v = (seed * 1103515245 + 12345) & 0x7fffffff;
    return (double)(v % 10000) / 10000.0;
}

static void setMaterial(float r, float g, float b, float a = 1.0f, float specPower = 48.0f)
{
    // Материал задаём вручную через glMaterial.
    // GL_COLOR_MATERIAL в Render() отключён, иначе OpenGL перезаписывает ambient/diffuse
    // обычным glColor, и сцена выглядит почти одинаково при включенном и выключенном свете.
    float amb[] = { r * 0.025f, g * 0.025f, b * 0.025f, a };
    float dif[] = { r, g, b, a };
    float spec[] = { 0.70f, 0.70f, 0.70f, a };

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, amb);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, dif);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, specPower);

    // Когда GL_LIGHTING выключен, OpenGL использует glColor напрямую.
    // Поэтому специально делаем режим без освещения плоским и более тёмным,
    // чтобы переключатель L был хорошо заметен на защите.
    if (lightning)
        glColor4f(r, g, b, a);
    else
        glColor4f(r * 0.30f, g * 0.30f, b * 0.30f, a);
}

static void disableAllSceneLights()
{
    glDisable(GL_LIGHT0);
    glDisable(GL_LIGHT1);
    glDisable(GL_LIGHT2);
    glDisable(GL_LIGHT3);
    glDisable(GL_LIGHT4);
    glDisable(GL_LIGHT5);
    glDisable(GL_LIGHT6);
    glDisable(GL_LIGHT7);
}

static void setupSceneLighting()
{
    // Полностью отключаем все старые источники света из каркаса лабораторной,
    // чтобы на сцену влиял только наш GL_LIGHT0.
    disableAllSceneLights();

    if (!lightning)
    {
        glDisable(GL_LIGHTING);
        return;
    }

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);

    // Минимальная глобальная подсветка: без источника света сцена почти не освещается.
    float globalAmbient[] = { 0.002f, 0.002f, 0.003f, 1.0f };
    float lightAmbient[]  = { 0.010f, 0.008f, 0.006f, 1.0f };
    float lightDiffuse[]  = { 1.05f, 0.88f, 0.62f, 1.0f };
    float lightSpecular[] = { 1.25f, 1.15f, 0.95f, 1.0f };
    float lightPos[]      = { sceneLightX, sceneLightY, sceneLightZ, 1.0f };

    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmbient);
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, lightSpecular);
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

    // Более заметное затухание, чтобы включение и выключение освещения было хорошо видно.
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 0.35f);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.28f);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.075f);
}

static void drawUnitCube()
{
    glBegin(GL_QUADS);

    glNormal3d(0, 0, 1);
    glTexCoord2d(0, 0); glVertex3d(-0.5, -0.5,  0.5);
    glTexCoord2d(1, 0); glVertex3d( 0.5, -0.5,  0.5);
    glTexCoord2d(1, 1); glVertex3d( 0.5,  0.5,  0.5);
    glTexCoord2d(0, 1); glVertex3d(-0.5,  0.5,  0.5);

    glNormal3d(0, 0, -1);
    glTexCoord2d(0, 0); glVertex3d( 0.5, -0.5, -0.5);
    glTexCoord2d(1, 0); glVertex3d(-0.5, -0.5, -0.5);
    glTexCoord2d(1, 1); glVertex3d(-0.5,  0.5, -0.5);
    glTexCoord2d(0, 1); glVertex3d( 0.5,  0.5, -0.5);

    glNormal3d(1, 0, 0);
    glTexCoord2d(0, 0); glVertex3d(0.5, -0.5,  0.5);
    glTexCoord2d(1, 0); glVertex3d(0.5, -0.5, -0.5);
    glTexCoord2d(1, 1); glVertex3d(0.5,  0.5, -0.5);
    glTexCoord2d(0, 1); glVertex3d(0.5,  0.5,  0.5);

    glNormal3d(-1, 0, 0);
    glTexCoord2d(0, 0); glVertex3d(-0.5, -0.5, -0.5);
    glTexCoord2d(1, 0); glVertex3d(-0.5, -0.5,  0.5);
    glTexCoord2d(1, 1); glVertex3d(-0.5,  0.5,  0.5);
    glTexCoord2d(0, 1); glVertex3d(-0.5,  0.5, -0.5);

    glNormal3d(0, 1, 0);
    glTexCoord2d(0, 0); glVertex3d(-0.5, 0.5,  0.5);
    glTexCoord2d(1, 0); glVertex3d( 0.5, 0.5,  0.5);
    glTexCoord2d(1, 1); glVertex3d( 0.5, 0.5, -0.5);
    glTexCoord2d(0, 1); glVertex3d(-0.5, 0.5, -0.5);

    glNormal3d(0, -1, 0);
    glTexCoord2d(0, 0); glVertex3d(-0.5, -0.5, -0.5);
    glTexCoord2d(1, 0); glVertex3d( 0.5, -0.5, -0.5);
    glTexCoord2d(1, 1); glVertex3d( 0.5, -0.5,  0.5);
    glTexCoord2d(0, 1); glVertex3d(-0.5, -0.5,  0.5);

    glEnd();
}

static void drawBox(double sx, double sy, double sz)
{
    glPushMatrix();
    glScaled(sx, sy, sz);
    drawUnitCube();
    glPopMatrix();
}

static void drawSphere(double radius, int slices = 24, int stacks = 12)
{
    GLUquadric* q = gluNewQuadric();
    gluQuadricNormals(q, GLU_SMOOTH);
    gluSphere(q, radius, slices, stacks);
    gluDeleteQuadric(q);
}

static void drawCylinderY(double radius, double height, int slices = 20)
{
    GLUquadric* q = gluNewQuadric();
    gluQuadricNormals(q, GLU_SMOOTH);

    glPushMatrix();
    glTranslated(0.0, -height * 0.5, 0.0);
    glRotated(-90.0, 1.0, 0.0, 0.0);
    gluCylinder(q, radius, radius, height, slices, 1);
    gluDisk(q, 0.0, radius, slices, 1);
    glTranslated(0.0, 0.0, height);
    gluDisk(q, 0.0, radius, slices, 1);
    glPopMatrix();

    gluDeleteQuadric(q);
}

static void drawConeY(double radius, double height, int slices = 20)
{
    GLUquadric* q = gluNewQuadric();
    gluQuadricNormals(q, GLU_SMOOTH);

    glPushMatrix();
    glTranslated(0.0, -height * 0.5, 0.0);
    glRotated(-90.0, 1.0, 0.0, 0.0);
    gluCylinder(q, radius, 0.0, height, slices, 1);
    gluDisk(q, 0.0, radius, slices, 1);
    glPopMatrix();

    gluDeleteQuadric(q);
}

static void drawBranch(double x, double y, double z, double rz, double rx, double length, double radius)
{
    glPushMatrix();
    glTranslated(x, y, z);
    glRotated(rz, 0.0, 0.0, 1.0);
    glRotated(rx, 1.0, 0.0, 0.0);
    glTranslated(0.0, length * 0.5, 0.0);
    drawCylinderY(radius, length, 16);
    glPopMatrix();
}

static void startSlash(SamuraiPose pose)
{
    samuraiPose = pose;
    slashTimer = SLASH_DURATION;
}

static void resetFruit(int i)
{
    if (i < 0 || i >= (int)fruits.size()) return;

    double side = (i % 2 == 0) ? -1.0 : 1.0;
    Fruit& f = fruits[i];
    f.x = side * (3.0 + rnd01(i + (int)(full_time * 10.0)) * 0.75);
    f.y = -0.82 + rnd01(i * 4 + 7) * 0.25;
    f.z = -0.70 + rnd01(i * 5 + 3) * 1.40;
    f.baseZ = f.z;
    f.vx = -side * (1.15 + rnd01(i * 8 + 2) * 0.55);
    f.vy = 2.65 + rnd01(i * 11 + 5) * 0.75;
    f.phase = full_time + i * 1.73;
    f.radius = 0.14 + 0.035 * (i % 3);
    f.type = i % 4;
}

static void initFruits()
{
    fruits.resize(7);
    for (int i = 0; i < (int)fruits.size(); ++i)
        resetFruit(i);
}

static bool fruitTouchedBySword(const Fruit& f)
{
    if (slashTimer <= 0.03) return false;

    // Хитбоксы теперь повторяют направление видимого клинка:
    // влево/вправо — почти горизонтальная полоса, вверх — вертикальная полоса.
    if (fabs(f.z) > 0.72)
        return false;

    if (samuraiPose == POSE_SLASH_LEFT)
        return f.x < 0.15 && f.x > -1.55 && fabs(f.y - 0.20) < 0.36 + f.radius;

    if (samuraiPose == POSE_SLASH_RIGHT)
        return f.x > -0.15 && f.x < 1.55 && fabs(f.y - 0.20) < 0.36 + f.radius;

    if (samuraiPose == POSE_SLASH_UP)
        return fabs(f.x) < 0.48 + f.radius && f.y > -0.25 && f.y < 1.65;

    return false;
}

static void updateGame(double delta_time)
{
    if (paused) return;

    if (slashTimer > 0.0)
    {
        slashTimer -= delta_time;
        if (slashTimer <= 0.0)
        {
            slashTimer = 0.0;
            samuraiPose = POSE_IDLE;
        }
    }

    for (int i = 0; i < (int)fruits.size(); ++i)
    {
        Fruit& f = fruits[i];
        f.x += f.vx * delta_time;
        f.y += f.vy * delta_time;
        f.vy -= GRAVITY * delta_time;
        f.z = f.baseZ + 0.18 * sin(full_time * 1.2 + f.phase);

        if (fruitTouchedBySword(f))
        {
            ++score;
            resetFruit(i);
            continue;
        }

        if (f.y < -1.25 || fabs(f.x) > 4.2)
        {
            ++missed;
            resetFruit(i);
        }
    }
}

// ------------------------------------------------------------
// Управление
// ------------------------------------------------------------

void switchModes(OpenGL* sender, KeyEventArg arg)
{
    int key = LOWORD(MapVirtualKeyA(arg.key, MAPVK_VK_TO_CHAR));
    if (key >= 'a' && key <= 'z') key -= 32;

    switch (key)
    {
    case 'L':
        lightning = !lightning;
        break;
    case 'T':
        texturing = !texturing;
        break;
    case 'A':
        alpha = !alpha;
        break;
    case 'P':
        paused = !paused;
        break;
    case 'R':
        score = 0;
        missed = 0;
        initFruits();
        samuraiPose = POSE_IDLE;
        slashTimer = 0.0;
        break;
    case 'J':
        startSlash(POSE_SLASH_LEFT);
        break;
    case 'K':
        startSlash(POSE_SLASH_RIGHT);
        break;
    case 'I':
        startSlash(POSE_SLASH_UP);
        break;
    }

    if (arg.key == VK_LEFT)
        startSlash(POSE_SLASH_LEFT);
    else if (arg.key == VK_RIGHT)
        startSlash(POSE_SLASH_RIGHT);
    else if (arg.key == VK_UP || arg.key == VK_SPACE)
        startSlash(POSE_SLASH_UP);
}

// ------------------------------------------------------------
// Рисование сцены
// ------------------------------------------------------------

static void drawTexturedGround()
{
    setMaterial(0.78f, 0.67f, 0.48f, 1.0f, 18.0f);

    if (texturing)
    {
        glEnable(GL_TEXTURE_2D);
        stankin_tex.Bind();
        // Текстура умножается на освещение, поэтому пол тоже реагирует на L и F.
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }
    else
    {
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glBegin(GL_QUADS);
    glNormal3d(0.0, 1.0, 0.0);
    glTexCoord2d(0.0, 0.0); glVertex3d(-4.0, -1.0, -2.6);
    glTexCoord2d(4.0, 0.0); glVertex3d( 4.0, -1.0, -2.6);
    glTexCoord2d(4.0, 4.0); glVertex3d( 4.0, -1.0,  2.6);
    glTexCoord2d(0.0, 4.0); glVertex3d(-4.0, -1.0,  2.6);
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void drawGate()
{
    setMaterial(0.55f, 0.05f, 0.04f, 1.0f, 22.0f);

    glPushMatrix();
    glTranslated(2.35, -0.45, -1.45);
    drawBox(0.18, 1.1, 0.18);
    glPopMatrix();

    glPushMatrix();
    glTranslated(3.05, -0.45, -1.45);
    drawBox(0.18, 1.1, 0.18);
    glPopMatrix();

    glPushMatrix();
    glTranslated(2.70, 0.18, -1.45);
    drawBox(1.0, 0.16, 0.22);
    glPopMatrix();

    setMaterial(0.12f, 0.05f, 0.04f, 1.0f, 12.0f);
    glPushMatrix();
    glTranslated(2.70, 0.39, -1.45);
    drawBox(1.18, 0.13, 0.26);
    glPopMatrix();
}

static void drawSakuraTree()
{
    glDisable(GL_TEXTURE_2D);

    setMaterial(0.34f, 0.17f, 0.07f, 1.0f, 20.0f);
    glPushMatrix();
    glTranslated(-2.15, -0.35, -0.55);
    drawCylinderY(0.16, 1.30, 24);
    glPopMatrix();

    drawBranch(-2.15, 0.18, -0.55, -45.0,  15.0, 0.78, 0.065);
    drawBranch(-2.12, 0.35, -0.55,  50.0, -25.0, 0.72, 0.058);
    drawBranch(-2.15, 0.48, -0.55,  10.0,  45.0, 0.62, 0.050);
    drawBranch(-2.18, 0.23, -0.55, -15.0, -45.0, 0.56, 0.048);

    // Крона из простых сфер: объект собран из геометрических примитивов.
    for (int i = 0; i < 12; ++i)
    {
        double a = i * PI / 6.0;
        double r = 0.42 + 0.12 * sin(i * 1.7);
        double x = -2.15 + cos(a) * r * 0.75;
        double y = 0.82 + sin(i * 2.1) * 0.16;
        double z = -0.55 + sin(a) * r * 0.55;

        setMaterial(1.0f, 0.56f + 0.08f * (float)(i % 2), 0.78f, 1.0f, 12.0f);
        glPushMatrix();
        glTranslated(x, y, z);
        drawSphere(0.30 + 0.035 * (i % 3), 18, 10);
        glPopMatrix();
    }
}

static void drawPetal(double x, double y, double z, double angle, double size)
{
    glPushMatrix();
    glTranslated(x, y, z);
    glRotated(angle, 0.0, 1.0, 0.0);
    glRotated(angle * 0.37, 0.0, 0.0, 1.0);

    glBegin(GL_QUADS);
    glVertex3d(-size, 0.0, -size * 0.35);
    glVertex3d( size, 0.0, -size * 0.35);
    glVertex3d( size, 0.0,  size * 0.35);
    glVertex3d(-size, 0.0,  size * 0.35);
    glEnd();

    glPopMatrix();
}

static void drawTransparentPetals()
{
    if (!alpha) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);

    bool restoreLighting = glIsEnabled(GL_LIGHTING) == GL_TRUE;
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    setMaterial(1.0f, 0.60f, 0.82f, 0.46f, 6.0f);

    for (int i = 0; i < 44; ++i)
    {
        double fall = fmod(full_time * 0.22 + i * 0.071, 1.9);
        double y = 1.22 - fall;
        double x = -2.10 + 1.25 * sin(i * 0.73 + full_time * 0.62);
        double z = -0.48 + 0.75 * cos(i * 0.61 + full_time * 0.47);
        drawPetal(x, y, z, full_time * 115.0 + i * 31.0, 0.035 + 0.007 * (i % 3));
    }

    glDepthMask(GL_TRUE);
    if (restoreLighting) glEnable(GL_LIGHTING);
    glDisable(GL_BLEND);
}

static void drawFruit(const Fruit& f)
{
    glDisable(GL_TEXTURE_2D);

    if (f.type == 0) setMaterial(0.95f, 0.12f, 0.06f, 1.0f, 40.0f);       // яблоко
    if (f.type == 1) setMaterial(1.00f, 0.48f, 0.03f, 1.0f, 35.0f);       // апельсин
    if (f.type == 2) setMaterial(0.70f, 0.08f, 0.88f, 1.0f, 35.0f);       // слива
    if (f.type == 3) setMaterial(0.95f, 0.85f, 0.06f, 1.0f, 35.0f);       // лимон

    glPushMatrix();
    glTranslated(f.x, f.y, f.z);
    glRotated(full_time * 120.0 + f.phase * 90.0, 0.3, 1.0, 0.2);
    drawSphere(f.radius, 24, 12);

    setMaterial(0.18f, 0.10f, 0.03f, 1.0f, 10.0f);
    glPushMatrix();
    glTranslated(0.0, f.radius * 0.95, 0.0);
    glRotated(18.0, 1.0, 0.0, 0.0);
    drawCylinderY(f.radius * 0.10, f.radius * 0.65, 10);
    glPopMatrix();

    glPopMatrix();
}

static double angleForLocalY(double dx, double dy)
{
    // drawCylinderY/drawBox вытянуты вдоль локальной оси Y.
    // Эта формула поворачивает локальную Y точно в направление вектора (dx, dy).
    return atan2(dy, dx) * 180.0 / PI - 90.0;
}

static void drawCylinderBetweenXY(double x1, double y1, double x2, double y2, double z, double radius)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 0.0001) return;

    glPushMatrix();
    glTranslated((x1 + x2) * 0.5, (y1 + y2) * 0.5, z);
    glRotated(angleForLocalY(dx, dy), 0.0, 0.0, 1.0);
    drawCylinderY(radius, len, 14);
    glPopMatrix();
}

static void drawSwordXY(double startX, double startY, double endX, double endY, double z)
{
    double dx = endX - startX;
    double dy = endY - startY;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 0.0001) return;

    double ux = dx / len;
    double uy = dy / len;
    double angle = angleForLocalY(dx, dy);

    // Рукоять идет назад от точки руки.
    setMaterial(0.08f, 0.04f, 0.02f, 1.0f, 18.0f);
    glPushMatrix();
    glTranslated(startX - ux * 0.11, startY - uy * 0.11, z);
    glRotated(angle, 0.0, 0.0, 1.0);
    drawBox(0.08, 0.22, 0.06);
    glPopMatrix();

    // Гарда поперек клинка.
    setMaterial(0.94f, 0.82f, 0.24f, 1.0f, 70.0f);
    glPushMatrix();
    glTranslated(startX, startY, z);
    glRotated(angle, 0.0, 0.0, 1.0);
    drawBox(0.22, 0.035, 0.055);
    glPopMatrix();

    // Сам клинок строго направлен от start к end.
    setMaterial(0.78f, 0.84f, 0.88f, 1.0f, 120.0f);
    glPushMatrix();
    glTranslated((startX + endX) * 0.5, (startY + endY) * 0.5, z);
    glRotated(angle, 0.0, 0.0, 1.0);
    drawBox(0.035, len, 0.018);

    setMaterial(0.94f, 0.92f, 0.76f, 1.0f, 80.0f);
    glTranslated(0.0, len * 0.5 + 0.065, 0.0);
    drawConeY(0.035, 0.13, 16);
    glPopMatrix();
}

static void drawTrailRibbonXY(double x1, double y1, double x2, double y2,
                              double z, double halfWidth, double alphaValue)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 0.0001) return;

    double nx = -dy / len * halfWidth;
    double ny =  dx / len * halfWidth;

    glBegin(GL_TRIANGLE_STRIP);
    glColor4f(0.80f, 0.92f, 1.0f, (float)(alphaValue * 0.20));
    glVertex3d(x1 + nx, y1 + ny, z);
    glVertex3d(x1 - nx, y1 - ny, z);

    glColor4f(0.80f, 0.92f, 1.0f, (float)alphaValue);
    glVertex3d(x2 + nx, y2 + ny, z);
    glVertex3d(x2 - nx, y2 - ny, z);
    glEnd();
}

static void drawSlashTrail()
{
    if (!alpha || slashTimer <= 0.03) return;

    double k = slashTimer / SLASH_DURATION;
    double pulse = 0.55 + 0.45 * sin((1.0 - k) * PI);
    double a = 0.34 * pulse;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);

    // Прозрачный след не должен записываться в depth-buffer.
    // Иначе он "вырезает" фрукты, которые рисуются рядом/после него.
    glDepthMask(GL_FALSE);

    // След теперь совпадает с направлением удара:
    // влево/вправо — горизонтальная полоса, вверх — вертикальная.
    if (samuraiPose == POSE_SLASH_LEFT)
        drawTrailRibbonXY(0.18, 0.20, -1.36, 0.20, 0.08, 0.13, a);
    else if (samuraiPose == POSE_SLASH_RIGHT)
        drawTrailRibbonXY(-0.18, 0.20, 1.36, 0.20, 0.08, 0.13, a);
    else if (samuraiPose == POSE_SLASH_UP)
        drawTrailRibbonXY(0.04, -0.18, 0.04, 1.28, 0.08, 0.12, a);

    glDepthMask(GL_TRUE);

    if (lightning) glEnable(GL_LIGHTING);
    glDisable(GL_BLEND);
}

static void drawSamurai()
{
    glDisable(GL_TEXTURE_2D);

    double k = (slashTimer > 0.0) ? (slashTimer / SLASH_DURATION) : 0.0;
    double wobble = (slashTimer > 0.0) ? 0.025 * sin((1.0 - k) * PI * 2.0) : 0.0;

    // Координаты руки и кончика катаны задаются явно.
    // Поэтому удары влево/вправо теперь не "диагональные", а строго горизонтальные.
    double handX = 0.33;
    double handY = 0.78;
    double tipX = 0.58;
    double tipY = 1.42;

    if (samuraiPose == POSE_SLASH_LEFT)
    {
        handX = 0.06;
        handY = 1.15 + wobble;
        tipX = -1.36;
        tipY = handY;
    }
    else if (samuraiPose == POSE_SLASH_RIGHT)
    {
        handX = -0.06;
        handY = 1.15 + wobble;
        tipX = 1.36;
        tipY = handY;
    }
    else if (samuraiPose == POSE_SLASH_UP)
    {
        handX = 0.08 + wobble;
        handY = 1.02;
        tipX = handX;
        tipY = 1.94;
    }

    glPushMatrix();
    glTranslated(0.0, -0.95, 0.0);

    // Ноги
    setMaterial(0.04f, 0.04f, 0.08f, 1.0f, 20.0f);
    glPushMatrix(); glTranslated(-0.12, 0.22, 0.0); drawBox(0.13, 0.45, 0.14); glPopMatrix();
    glPushMatrix(); glTranslated( 0.12, 0.22, 0.0); drawBox(0.13, 0.45, 0.14); glPopMatrix();

    // Туловище и пояс
    setMaterial(0.10f, 0.12f, 0.18f, 1.0f, 28.0f);
    glPushMatrix(); glTranslated(0.0, 0.66, 0.0); drawBox(0.42, 0.62, 0.20); glPopMatrix();

    setMaterial(0.72f, 0.05f, 0.06f, 1.0f, 20.0f);
    glPushMatrix(); glTranslated(0.0, 0.52, 0.01); drawBox(0.48, 0.08, 0.23); glPopMatrix();

    // Голова, волосы, шляпа
    setMaterial(0.92f, 0.70f, 0.48f, 1.0f, 22.0f);
    glPushMatrix(); glTranslated(0.0, 1.08, 0.0); drawSphere(0.15, 20, 10); glPopMatrix();

    setMaterial(0.02f, 0.015f, 0.012f, 1.0f, 10.0f);
    glPushMatrix(); glTranslated(0.0, 1.15, -0.02); drawSphere(0.105, 16, 8); glPopMatrix();

    setMaterial(0.31f, 0.21f, 0.08f, 1.0f, 12.0f);
    glPushMatrix();
    glTranslated(0.0, 1.22, 0.0);
    glScaled(1.0, 0.16, 1.0);
    drawSphere(0.34, 24, 8);
    glPopMatrix();

    setMaterial(0.34f, 0.23f, 0.09f, 1.0f, 12.0f);
    glPushMatrix(); glTranslated(0.0, 1.31, 0.0); drawConeY(0.18, 0.22, 24); glPopMatrix();

    // Левая рука
    setMaterial(0.10f, 0.12f, 0.18f, 1.0f, 20.0f);
    drawCylinderBetweenXY(-0.24, 0.82, -0.38, 0.55, 0.00, 0.045);

    // Правая рука смотрит прямо к рукояти катаны.
    setMaterial(0.10f, 0.12f, 0.18f, 1.0f, 20.0f);
    drawCylinderBetweenXY(0.24, 0.82, handX, handY, 0.03, 0.045);

    // Кисть
    setMaterial(0.92f, 0.70f, 0.48f, 1.0f, 22.0f);
    glPushMatrix();
    glTranslated(handX, handY, 0.03);
    drawSphere(0.055, 12, 6);
    glPopMatrix();

    // Катана. При J/← она строго влево, при K/→ строго вправо.
    drawSwordXY(handX, handY, tipX, tipY, 0.05);

    glPopMatrix();
}

static void drawMoonAndBackground()
{
    bool restoreLighting = glIsEnabled(GL_LIGHTING) == GL_TRUE;
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);

    setMaterial(0.10f, 0.12f, 0.20f, 1.0f, 1.0f);
    glPushMatrix();
    glTranslated(0.0, 0.8, -2.55);
    drawBox(7.0, 3.6, 0.03);
    glPopMatrix();

    setMaterial(0.98f, 0.90f, 0.62f, 1.0f, 6.0f);
    glPushMatrix();
    glTranslated(2.55, 1.55, -2.45);
    drawSphere(0.23, 32, 16);
    glPopMatrix();

    if (restoreLighting) glEnable(GL_LIGHTING);
}

// ------------------------------------------------------------
// Инициализация и основной рендер
// ------------------------------------------------------------

void initRender()
{
    stankin_tex.LoadTexture("textures/stankin.png");

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glDisable(GL_COLOR_MATERIAL);

    camera.caclulateCameraPos();

    // Камера из исходного каркаса
    gl.WheelEvent.reaction(&camera, &Camera::Zoom);
    gl.MouseMovieEvent.reaction(&camera, &Camera::MouseMovie);
    gl.MouseLeaveEvent.reaction(&camera, &Camera::MouseLeave);
    gl.MouseLdownEvent.reaction(&camera, &Camera::MouseStartDrag);
    gl.MouseLupEvent.reaction(&camera, &Camera::MouseStopDrag);


    gl.KeyDownEvent.reaction(switchModes);

    text.setSize(620, 260);

    camera.setPosition(3.6, 2.25, 3.0);
    light.SetPosition(sceneLightX, sceneLightY, sceneLightZ);

    initFruits();
}

void Render(double delta_time)
{
    full_time += delta_time;
    updateGame(delta_time);

    // Источник света фиксированный. Перемещение света отключено.
    light.SetPosition(sceneLightX, sceneLightY, sceneLightZ);

    camera.SetUpCamera();
    glGetFloatv(GL_MODELVIEW_MATRIX, view_matrix);
    setupSceneLighting();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glDisable(GL_COLOR_MATERIAL);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    setupSceneLighting();

    glShadeModel(GL_SMOOTH);

    // Оси можно отключить, если нужна более "игровая" картинка.
    gl.DrawAxes();

    drawMoonAndBackground();
    drawTexturedGround();
    drawGate();
    drawSakuraTree();
    drawSamurai();

    for (int i = 0; i < (int)fruits.size(); ++i)
        drawFruit(fruits[i]);

    // Прозрачные объекты рисуем после непрозрачных, чтобы они не портили z-buffer.
    drawSlashTrail();
    drawTransparentPetals();

    // ---------------------- HUD ----------------------
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, gl.getWidth() - 1, 0, gl.getHeight() - 1, 0, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    std::wstringstream ss;
    ss << L"Samurai Fruit Cutter\n"
       << L"Счёт: " << score << L"   Пропущено: " << missed << L"\n"
       << L"Удар: SPACE/↑ - вверх, ←/J - влево, →/K - вправо, I - вверх\n"
       << L"T - " << (texturing ? L"[вкл] выкл" : L"вкл [выкл]") << L" текстура пола\n"
       << L"L - " << (lightning ? L"[вкл] выкл" : L"вкл [выкл]") << L" освещение\n"
       << L"A - " << (alpha ? L"[вкл] выкл" : L"вкл [выкл]") << L" альфа-наложение лепестков/следа\n"
       << L"P - пауза, R - сброс\n"
       << L"Состояние самурая: "
       << (samuraiPose == POSE_IDLE ? L"ожидание" :
           samuraiPose == POSE_SLASH_LEFT ? L"удар влево" :
           samuraiPose == POSE_SLASH_RIGHT ? L"удар вправо" : L"удар вверх")
       << L"\nВремя: " << std::fixed << std::setprecision(2) << full_time;

    text.setPosition(10, gl.getHeight() - 10 - 260);
    text.setText(ss.str().c_str());
    text.Draw();

    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}
