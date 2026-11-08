// ski32_decomp.cpp : Defines the entry point for the application.
//

#define NOMINMAX

#include "skifree_decomp.h"
#include "consts.h"
#include "data.h"
#include "embedded_resources.h"
#include "resource.h"
#include <SDL_image.h>
#include <stdio.h>
#include <string.h>

#define ski_assert(exp, line) (void)((exp) || (assertFailed(sourceFilename, line), 0)) // TODO remove need for src param.

// Velocities in the decomp are pixels per 40 ms update. Keep that unit, but
// integrate it once per presented frame using this scale and sub-pixel carry.
static float physicsFrameScale = 1.0f;
static float actorMovementRemainderX[NUM_ACTORS];
static float actorMovementRemainderY[NUM_ACTORS];
static float actorMovementRemainderAir[NUM_ACTORS];
static float actorCounterRemainder[NUM_ACTORS];
static float permObjectMovementRemainderX[NUM_PERM_OBJECTS];
static float permObjectMovementRemainderY[NUM_PERM_OBJECTS];
static float permObjectMovementRemainderAir[NUM_PERM_OBJECTS];
static DWORD logicTimeAccumulator;
static BOOL perFramePhysicsEnabled = TRUE;

static void updateMovementOnly(void);

static int takeScaledMovement(short velocity, float* remainder) {
    int movement;

    *remainder += velocity * physicsFrameScale;
    movement = (int)*remainder;
    *remainder -= movement;
    return movement;
}

// int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
int main(int argc, char* argv[]) {
    int iVar1;
    BOOL retVal;
    // MSG msg;

    SDL_Event event;

    for (iVar1 = 1; iVar1 < argc; iVar1++) {
        if (strcmp(argv[iVar1], "--no-per-frame-physics") == 0) {
            perFramePhysicsEnabled = FALSE;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        printf("failed to init\n");
        return 1;
    }

    TTF_Init();

    // todo
    // iVar1 = strcmp(lpCmdLine, s_nosound_0040c0fc);
    // if (iVar1 == 0)
    // {
    //     isSoundDisabled = 1;
    // }
    retVal = allocateMemory();
    if (retVal == 0) {
        return 0;
    }
    retVal = resetGame();
    if (retVal == 0) {
        return 0;
    }
    retVal = initWindows(/*hInstance, hPrevInstance, nCmdShow*/);
    if (retVal == 0) {
        return 0;
    }
    iVar1 = setupGame();
    if (iVar1 == 0) {
        SDL_DestroyWindow(hSkiMainWnd);
        cleanupSound();
        return 0;
    }
    // iVar1 = GetMessageA(&msg, NULL, 0, 0);
    // while (iVar1 != 0)
    // {
    //     TranslateMessage(&msg);
    //     DispatchMessageA(&msg);
    //     iVar1 = GetMessageA(&msg, NULL, 0, 0);
    // }

    paintStatusWindow(NULL);

    int is_running = 1;
    while (is_running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                is_running = 0;
                break;
            case SDL_WINDOWEVENT:
                HandleWindowMessage(&event);
                break;
            case SDL_KEYDOWN:
                handleKeydownMessage(&event);
                break;
            }
        }

        if (isGameTimerRunning) {
            DWORD ticks = SDL_GetTicks();
            if (perFramePhysicsEnabled && currentTickCount != ticks) {
                timerUpdateFunc();
            } else if (!perFramePhysicsEnabled && currentTickCount + updateTimerDurationMillis < ticks) {
                timerUpdateFunc();
            }
        }

        mainWindowPaint(hSkiMainWnd);
    }
    cleanupSound();
    return 0;
}

int max_(int a, int b) {
    if (a > b) {
        return a;
    }
    return b;
}

int min_(int a, int b) {
    if (b > a) {
        return a;
    }
    return b;
}

void timerUpdateFunc(void) {
    DWORD ticks;

    ticks = SDL_GetTicks();
    timerFrameDurationInMillis = ticks - currentTickCount;
    prevTickCount = currentTickCount;
    currentTickCount = ticks;
    if (perFramePhysicsEnabled) {
        physicsFrameScale = (float)timerFrameDurationInMillis / (float)updateTimerDurationMillis;
        logicTimeAccumulator += timerFrameDurationInMillis;
        if (logicTimeAccumulator >= (DWORD)updateTimerDurationMillis) {
            logicTimeAccumulator -= updateTimerDurationMillis;
            updateGameState();
        } else {
            updateMovementOnly();
        }
    } else {
        physicsFrameScale = 1.0f;
        updateGameState();
    }
    drawWindow(mainWindowDC, &windowClientRect);
    redrawRequired = TRUE;
    if (currentTickCount - statusWindowLastUpdateTime > 327) {
        formatAndPrintStatusStrings(statusWindowDC);
    }
}

void assertFailedDialog(LPCSTR lpCaption, LPCSTR lpText) {
    // int iVar1;
    // iVar1 = MessageBoxA((HWND)0x0, lpText, lpCaption, 0x31);
    // if (iVar1 == IDCANCEL)
    // {
    //     DestroyWindow(hSkiMainWnd);
    // }
    printf("Assert failed: %s\n", lpText);
    abort();
}

void assertFailed(const char* srcFilename, int lineNumber) {
    char local_20[32];

    sprintf(local_20, s_assertErrorFormat, srcFilename, lineNumber);
    assertFailedDialog(s_Assertion_Failed_0040c0a8, local_20);
    togglePausedState();
}

BOOL doRectsOverlap(RECT* rect1, RECT* rect2) {
    ski_assert(rect1 != NULL, 352);
    ski_assert(rect2 != NULL, 353);

    if ((((rect2->left < rect1->right) && (rect1->left < rect2->right)) && (rect2->top < rect1->bottom)) && (rect1->top < rect2->bottom)) {
        return TRUE;
    }
    return FALSE;
}

BOOL areRectanglesEqual(RECT* rect1, RECT* rect2) {
    ski_assert(rect1 != NULL, 381);
    ski_assert(rect2 != NULL, 382);

    if ((((rect1->top == rect2->top) && (rect1->left == rect2->left)) && (rect1->right == rect2->right)) && (rect1->bottom == rect2->bottom)) {
        return TRUE;
    }
    return FALSE;
}

char* getCachedString(uint32_t stringIdx) {
    // if (stringCache[stringIdx] == NULL)
    // {
    //     length = LoadStringA(skiFreeHInstance, stringIdx, buf, 0xff);
    //     buf[length] = '\0';
    //     pcVar1 = (char *)LocalAlloc(0, length + 1);
    //     stringCache[stringIdx] = pcVar1;
    //     if (stringCache[stringIdx] == NULL)
    //     {
    //         return s_out_o_memory;
    //     }
    //     lstrcpyA(stringCache[stringIdx], buf);
    // }
    return STRINGTABLE[stringIdx - 1];
}

int formatElapsedTime(int totalMillis, char* outputString) {
    int iVar1;
    char* pcVar2;
    uint32_t uVar3;
    uint32_t uVar4;
    uint32_t uVar5;
    uint32_t uVar6;

    uVar6 = (totalMillis % 1000 & 0xffffU) / 10;
    uVar5 = (totalMillis / 1000) % 60 & 0xffff;
    iVar1 = (totalMillis / 1000) / 60;
    uVar3 = iVar1 % 60 & 0xffff;
    uVar4 = iVar1 / 60 & 0xffff;
    pcVar2 = getCachedString(IDS_TIME_FORMAT);
    sprintf(outputString, pcVar2, uVar4, uVar3, uVar5, uVar6);
    return strlen(outputString);
}

void drawText(HDC hdc, LPCSTR textStr, short x, short* y, int textLen) {
    // TextOutA(hdc, (int)x, (int)*y, textStr, textLen);

    SDL_Surface* surface;
    SDL_Color textColor = { 0, 0, 0, 0 };

    surface = TTF_RenderUTF8_Blended_Wrapped(statusWindowFont, textStr, textColor, 255);
    SDL_Rect rect;
    rect.x = x;
    rect.y = *y;
    rect.w = surface->w;
    rect.h = surface->h;
    SDL_BlitSurface(surface, NULL, statusWindowSurface, &rect);
    SDL_FreeSurface(surface);
    *y = *y + textLineHeight;
}

void HandleWindowMessage(SDL_Event* e) {
    switch (e->window.event) {
    case SDL_WINDOWEVENT_FOCUS_GAINED:
        mainWndActivationFlags = 1;
        updateWindowsActiveStatus();
        break;

    case SDL_WINDOWEVENT_FOCUS_LOST:
        mainWndActivationFlags = 0;
        updateWindowsActiveStatus();
        break;

    case SDL_WINDOWEVENT_MINIMIZED:
        isMinimised = 1;
        break;

    case SDL_WINDOWEVENT_RESTORED:
        isMinimised = 0;
        break;

    case SDL_WINDOWEVENT_SIZE_CHANGED:
        updateWindowSize(hSkiMainWnd);
        break;
    }
}

// TODO not byte accurate.
BOOL allocateMemory() {
    sprites = malloc(NUM_SPRITES * sizeof(Sprite));
    actors = malloc(NUM_ACTORS * sizeof(Actor));
    permObjects = malloc(NUM_PERM_OBJECTS * sizeof(PermObject));

    if (!actors || !sprites || !permObjects) {
        showErrorMessage(s_insufficient_local_memory);
        return FALSE;
    }
    // for (i = 0; i < NUM_STRINGS; i++)
    // {
    //     stringCache[i] = NULL;
    // }
    return TRUE;
}

// TODO not byte perfect
Actor* updateActorType1_Beginner(Actor* actor) {
    int random_result;
    Actor* pAVar2;
    uint32_t actorFrameNo;

    actorFrameNo = actor->frameNo;
    if (actor == NULL) {
        assertFailed(sourceFilename, 2130);
    }
    if (actor->typeMaybe != 1) {
        assertFailed(sourceFilename, 2131);
    }
    if (actorFrameNo < 25) {
        pAVar2 = updateActorPositionWithVelocityMaybe(actor);
        if (actorFrameNo - 22 >= 5) {
            assertFailed(sourceFilename, 2135);
        }
        pAVar2 = updateActorVelMaybe(pAVar2, &beginnerActorMovementTbl[actorFrameNo - 22]);
        if (ski_random(0xc) == 0) {
            random_result = ski_random(3);
            if (random_result == 0) {
                actorFrameNo = 0x16;
            } else if (random_result == 1) {
                return setActorFrameNo(pAVar2, 0x17);
            } else if (random_result == 2) {
                return setActorFrameNo(pAVar2, 0x18);
            }
        }
        return setActorFrameNo(pAVar2, actorFrameNo);
    }
    return actor;
}

// TODO not byte perfect
Actor* updateActorType2_dog(Actor* actor) {
    short sVar1;
    short rand_result;
    Actor* new_actor;
    short newY;
    uint32_t actorFrameNo;
    short inAir;

    actorFrameNo = actor->frameNo;
    if (actor->typeMaybe != 2) {
        assertFailed(sourceFilename, 2162);
    }
    switch (actorFrameNo) {
    case 0x1b:
        rand_result = ski_random(3);
        actor->verticalVelocityMaybe = rand_result - 1;
        new_actor = updateActorPositionWithVelocityMaybe(actor);
        return setActorFrameNo(new_actor, 0x1c);
    case 0x1c:
        actor->HorizontalVelMaybe = 4;
        new_actor = updateActorPositionWithVelocityMaybe(actor);
        return setActorFrameNo(new_actor, 0x1b);
    case 0x1d:
        actor->verticalVelocityMaybe = 0;
        actor->HorizontalVelMaybe = 0;
        rand_result = ski_random(0x20);
        new_actor = updateActorPositionWithVelocityMaybe(actor);
        return setActorFrameNo(new_actor, rand_result != 0 ? 0x1b + 3 : 0x1b);
    case 0x1e:
        rand_result = ski_random(100);
        if (rand_result != 0) {
            new_actor = updateActorPositionWithVelocityMaybe(actor);
            return setActorFrameNo(new_actor, 0x1d);
        }
        inAir = actor->isInAir;
        sVar1 = actor->xPosMaybe;
        newY = actor->yPosMaybe + -2;
        /* dog wee */
        new_actor = addActorOfTypeWithSpriteIdx(ACTOR_TYPE_17_SIGN, 0x52);
        updateActorPositionMaybe(new_actor, (short)(sVar1 - 4), newY, inAir);
        actorFrameNo = 0x1b;
        playSound(&sound_8);
    }
    new_actor = updateActorPositionWithVelocityMaybe(actor);
    return setActorFrameNo(new_actor, actorFrameNo);
}

Actor* updateActorType9_treeOnFire(Actor* actor) {
    int frameNo = actor->frameNo;
    ski_assert(actor->typeMaybe == 9, 2204);
    ski_assert(frameNo >= 0x38, 2205);
    ski_assert(frameNo < 0x3c, 2206);

    frameNo++;
    if (frameNo >= 0x3c) {
        frameNo = 0x38;
    }
    return setActorFrameNo(actor, frameNo);
}

Actor* getLinkedActorIfExists(Actor* actor) {
    ski_assert(actor, 965);
    if (actor->linkedActor == NULL) {
        return actor;
    }
    return actor->linkedActor;
}

int showErrorMessage(LPCSTR text) {
    // return MessageBoxA(NULL, text, getCachedString(IDS_TITLE), 0x30);
    return SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, getCachedString(IDS_TITLE), text, hSkiMainWnd);
}

Actor* addActorOfTypeWithSpriteIdx(int actorType, uint16_t spriteIdx) {
    Actor* actor;

    actor = getFreeActor();
    if (actor != NULL) {
        ski_assert(actorType >= 0, 1403);
        ski_assert(actorType < 0x12, 1404);

        actor->typeMaybe = actorType;
        actor = actorSetSpriteIdx(actor, spriteIdx);
        return actor;
    }
    return actor;
}

void setupGameTitleActors() {
    Actor* actor;
    short x;
    short y;

    y = playerY;
    x = -(sprites[0x35].width / 2) - 40;

    actor = addActorOfTypeWithSpriteIdx(ACTOR_TYPE_17_SIGN, 0x35);
    updateActorPositionMaybe(actor, x, y, 0);

    y = y + sprites[0x36].height + 4;
    actor = addActorOfTypeWithSpriteIdx(ACTOR_TYPE_17_SIGN, 0x36);
    updateActorPositionMaybe(actor, x, y, 0);
    x = sprites[0x37].width;
    if (sprites[0x37].width <= sprites[0x38].width) {
        x = sprites[0x38].width;
    }
    y = sprites[0x37].height;
    actor = addActorOfTypeWithSpriteIdx(ACTOR_TYPE_17_SIGN, 0x37);
    updateActorPositionMaybe(actor, x, y, 0);

    y = y + sprites[0x38].height + 4;
    actor = addActorOfTypeWithSpriteIdx(ACTOR_TYPE_17_SIGN, 0x38);
    updateActorPositionMaybe(actor, x, y, 0);
}

/* WARNING: Removing unreachable block (ram,0x004053c9) */

int initWindows() {
    // short windowWidth;
    //  uint32_t uVar2;
    //  BOOL BVar3;
    int nHeight;
    char* lpWindowName;
    int nWidth;

    SCREEN_WIDTH = 1280;
    SCREEN_HEIGHT = 1024;

    isPaused = 0;
    // isMinimised = 1;
    isMinimised = 0;
    mainWndActivationFlags = 0;
    inputEnabled = 0;
    skierScreenXOffset = 0;
    skierScreenYOffset = 0;
    // hSkiMainWnd = FindWindowA("SkiMain", (LPCSTR)0x0);
    // if (hSkiMainWnd != (HWND)0x0)
    // {
    //     SetWindowPos(hSkiMainWnd, (HWND)0x0, 0, 0, 0, 0, 3);
    //     BVar3 = IsIconic(hSkiMainWnd);
    //     if (BVar3 != 0)
    //     {
    //         OpenIcon(hSkiMainWnd);
    //     }
    //     hSkiMainWnd = (HWND)0x0;
    //     return 0;
    // }
    // timerCallbackFuncPtr = timerCallbackFunc;
    if ((isSoundDisabled == 0) && (loadSoundFunc() != 0)) {
        loadSound(1, &sound_1);
        loadSound(2, &sound_2);
        loadSound(3, &sound_3);
        loadSound(4, &sound_4);
        loadSound(5, &sound_5);
        loadSound(6, &sound_6);
        loadSound(9, &sound_9);
        loadSound(7, &sound_7);
        loadSound(8, &sound_8);
    }
    windowWidth = SCREEN_WIDTH;
    if (SCREEN_HEIGHT <= SCREEN_WIDTH) {
        windowWidth = SCREEN_HEIGHT;
    }
    nWidth = windowWidth;
    nHeight = SCREEN_HEIGHT;
    lpWindowName = getCachedString(IDS_TITLE);

    // original code was asking for overall window size, here we have to convert it
    // to client size to get the correct original ratio
    nWidth = 1008;
    nHeight = nWidth * 0.97718253968254f;

    hSkiMainWnd = SDL_CreateWindow(lpWindowName,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        nWidth, nHeight,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    renderer = SDL_CreateRenderer(hSkiMainWnd, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        fprintf(stderr, "Could not create a vsynced renderer: %s\n", SDL_GetError());
        renderer = SDL_CreateRenderer(hSkiMainWnd, -1, SDL_RENDERER_ACCELERATED);
    }
    if (renderer == NULL) {
        fprintf(stderr, "Could not create an accelerated renderer: %s\n", SDL_GetError());
        return 0;
    }

    SDL_RendererInfo info;
    SDL_GetRendererInfo(renderer, &info);
    printf("Renderer name: %s\n", info.name);
    printf("VSync: %s\n", (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0 ? "enabled" : "unavailable");
    printf("Texture formats:\n");
    for (int i = 0; i < info.num_texture_formats; i++) {
        printf("%s\n", SDL_GetPixelFormatName(info.texture_formats[i]));
    }

    SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 0xFF);

    calculateStatusWindowDimensions(hSkiStatusWnd);
    statusWindowTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, statusWindowTotalTextWidth, statusWindowHeight);

    if (loadBitmaps(hSkiMainWnd) == 0) {
        return 0;
    }
    updateWindowSize(hSkiMainWnd);
    return 1;
}

BOOL loadSoundFunc() {
    sndPlaySoundAFuncPtr = NULL; // sndPlaySoundA;
    return (sndPlaySoundAFuncPtr != NULL);
}

BOOL loadSound(uint32_t resourceId, Sound* sound) {
    // HRSRC hResInfo;
    // HGLOBAL pvVar1;
    // LPVOID pvVar2;

    // hResInfo = FindResourceA(skiFreeHInstance, MAKEINTRESOURCE(resourceId), "WAVE");
    // sound->soundResource = hResInfo;
    // if (hResInfo != NULL)
    // {
    //     pvVar1 = LoadResource(skiFreeHInstance, hResInfo);
    //     sound->soundResource = pvVar1;
    // }
    // if (sound->soundResource != NULL)
    // {
    //     pvVar2 = LockResource(sound->soundResource);
    //     sound->soundData = pvVar2;
    //     return TRUE;
    // }
    sound->soundData = NULL;
    return FALSE;
}

uint16_t getSpriteIdxForActorType(int actorType) {
    int rand_result;

    switch (actorType) {
    case ACTOR_TYPE_11_MOGULS:
        return 0x1b;

    case ACTOR_TYPE_13_TREE:
        rand_result = ski_random(8);
        // TODO bytes here don't match exactly
        if (rand_result == 0) {
            return 0x32;
        }
        if (rand_result == 1) {
            return 0x33;
        }
        return 0x31;

    case ACTOR_TYPE_14_ROCK_STUMP:
        return ski_random(4) != 0 ? 0x2d : 0x2e;

    case ACTOR_TYPE_15_BUMP:
        return ski_random(3) != 0 ? 0x2f : 0x30;

    case ACTOR_TYPE_16_JUMP:
        return 0x34;

    default:
        assertFailed(sourceFilename, 1571);
        return 0;
    }
}

void playSound(Sound* sound) {
    // if (isSoundDisabled == 0)
    // {
    //     if ((sound->soundData == NULL) && (sound->soundResource != NULL))
    //     {
    //         sound->soundData = LockResource(sound->soundResource);
    //     }
    //     if ((sound->soundData != NULL) && (sndPlaySoundAFuncPtr != NULL))
    //     {
    //         (*sndPlaySoundAFuncPtr)(sound->soundData, SND_ASYNC | SND_MEMORY);
    //     }
    // }
}
// TODO problems in byte matching due to deadcode removal.
Actor* updateActorPositionWithVelocityMaybe(Actor* actor) {
    short newX;
    short newY;
    short inAir;
    int actorIndex;
    int counterUpdates;
    int speedMultiplier;

    ski_assert(actor, 1061);

    actorIndex = (int)(actor - actors);
    speedMultiplier = isTurboMode != 0 ? 2 : 1;
    newX = actor->xPosMaybe + takeScaledMovement(actor->HorizontalVelMaybe * speedMultiplier, &actorMovementRemainderX[actorIndex]);
    newY = actor->yPosMaybe + takeScaledMovement(actor->verticalVelocityMaybe * speedMultiplier, &actorMovementRemainderY[actorIndex]);
    inAir = actor->isInAir + takeScaledMovement(actor->inAirCounter * speedMultiplier, &actorMovementRemainderAir[actorIndex]);

    if (inAir > 0) {
        actorCounterRemainder[actorIndex] += physicsFrameScale;
        counterUpdates = (int)actorCounterRemainder[actorIndex];
        actorCounterRemainder[actorIndex] -= counterUpdates;
        actor->inAirCounter -= counterUpdates;
        return updateActorPositionMaybe(actor, newX, newY, inAir);
    }
    actor->inAirCounter = 0;
    actorCounterRemainder[actorIndex] = 0.0f;
    return updateActorPositionMaybe(actor, newX, newY, 0);
}

void startGameTimer() {
    if (hSkiMainWnd && !isGameTimerRunning && !isPaused) {
        isGameTimerRunning = TRUE;
        currentTickCount = SDL_GetTicks();
        logicTimeAccumulator = 0;
        if ((isSsGameMode != 0) || (isGsGameMode != 0)) {
            timedGameRelated = timedGameRelated + (currentTickCount - pauseStartTickCount);
        }
        // We do timing in the main loop instead of separate thread
        // SetTimer(hSkiMainWnd, 0x29a, updateTimerDurationMillis & 0xffff, timerCallbackFuncPtr);
    }
}

void cleanupSound() {
    if (isSoundDisabled == 0) {
        if (sndPlaySoundAFuncPtr != NULL) {
            (*sndPlaySoundAFuncPtr)(0, 0);
        }
        if (DAT_0040c78c != NULL) {
            // FreeLibrary(DAT_0040c78c);
        }
        freeSoundResource(&sound_1);
        freeSoundResource(&sound_2);
        freeSoundResource(&sound_3);
        freeSoundResource(&sound_4);
        freeSoundResource(&sound_5);
        freeSoundResource(&sound_6);
        freeSoundResource(&sound_9);
        freeSoundResource(&sound_7);
        freeSoundResource(&sound_8);
    }
}

void freeSoundResource(Sound* sound) {
    if (sound->soundData != NULL) {
        sound->soundData = NULL;
    }
    if (sound->soundResource != NULL) {
        // FreeResource(sound->soundResource);
        sound->soundResource = NULL;
    }
}

void togglePausedState() {
    char* str;

    isPaused = isGameTimerRunning;
    if (isGameTimerRunning != 0) {
        pauseGame();
        str = getCachedString(IDS_PAUSED);
        SDL_SetWindowTitle(hSkiMainWnd, str);
    } else {
        str = getCachedString(IDS_TITLE);
        SDL_SetWindowTitle(hSkiMainWnd, str);
        startGameTimer();
    }
}

void pauseGame() {
    if (hSkiMainWnd != NULL && isGameTimerRunning) {
        isGameTimerRunning = FALSE;
        pauseStartTickCount = currentTickCount;
    }
}

void enlargeRect(RECT* rect1, RECT* rect2) {
    ski_assert(rect2, 365);
    ski_assert(rect1, 366);

    if (rect2->left < rect1->left) {
        rect1->left = rect2->left;
    }
    if (rect2->right > rect1->right) {
        rect1->right = rect2->right;
    }
    if (rect2->top < rect1->top) {
        rect1->top = rect2->top;
    }
    if (rect2->bottom > rect1->bottom) {
        rect1->bottom = rect2->bottom;
    }
}

short ski_random(short maxValue) {
    return rand() % maxValue;
}

Actor* addActor(Actor* actor, BOOL insertBack) {
    Actor* targetActor;
    int sourceIndex;
    int targetIndex;

    targetActor = currentFreeActor;
    ski_assert(actor, 840);

    if (targetActor) {
        currentFreeActor = targetActor->next;

        memcpy(targetActor, actor, sizeof(Actor));
        targetIndex = (int)(targetActor - actors);
        if (actor == &blankTemplateActor) {
            actorMovementRemainderX[targetIndex] = 0.0f;
            actorMovementRemainderY[targetIndex] = 0.0f;
            actorMovementRemainderAir[targetIndex] = 0.0f;
            actorCounterRemainder[targetIndex] = 0.0f;
        } else {
            sourceIndex = (int)(actor - actors);
            actorMovementRemainderX[targetIndex] = actorMovementRemainderX[sourceIndex];
            actorMovementRemainderY[targetIndex] = actorMovementRemainderY[sourceIndex];
            actorMovementRemainderAir[targetIndex] = actorMovementRemainderAir[sourceIndex];
            actorCounterRemainder[targetIndex] = actorCounterRemainder[sourceIndex];
        }

        targetActor->permObject = NULL;
        if (insertBack) {
            targetActor->next = actor->next;
            actor->next = targetActor;
        } else {
            targetActor->next = actorListPtr;
            actorListPtr = targetActor;
        }
        return targetActor;
    } else {
        assertFailed(sourceFilename, 857);
    }

    return targetActor; // TODO fixme the original does `MOV EAX, EBX` but we seem to be doing `XOR EAX, EAX`
}

void addStylePoints(int points) {
    if (isFsGameMode != 0) {
        stylePoints = stylePoints + points;
    }
}

Actor* getFreeActor() {
    Actor* actor;

    blankTemplateActor.spritePtr = sprites;
    actor = addActor(&blankTemplateActor, 0);
    return actor;
}

Actor* addActorOfType(int actorType, uint32_t frameNo) {
    Actor* actor;

    actor = getFreeActor();
    if (actor != NULL) {
        ski_assert(actorType >= 0, 1388);
        ski_assert(actorType < 0x12, 1389);

        actor->typeMaybe = actorType;
        actor = setActorFrameNo(actor, frameNo);
    }
    return actor;
}

void handleGameReset() {
    if (resetGame()) {
        if (isPaused != 0) {
            togglePausedState();
        }
        // InvalidateRect(hSkiMainWnd, NULL, TRUE);
        if (setupGame()) {
            // UpdateWindow(hSkiMainWnd);
            return;
        }
    }
    SDL_DestroyWindow(hSkiMainWnd);
}

void handleCharMessage(uint32_t charCode) {
    switch (charCode) {
    case 'X':
        /* 'X' */
        if (playerActor) {
            updateActorPositionMaybe(playerActor, (short)(playerActor->xPosMaybe - 2), playerActor->yPosMaybe, playerActor->isInAir);
            return;
        }
        break;
    case 'Y':
        /* 'Y' */
        if (playerActor) {
            updateActorPositionMaybe(playerActor, playerActor->xPosMaybe, (short)(playerActor->yPosMaybe + -2), playerActor->isInAir);
        }
        break;
    case 'f':
        isTurboMode = (isTurboMode == 0);
        /* 'f' key */
        return;
    case 'r':
        /* 'r' */
        drawWindow(mainWindowDC, &windowClientRect);
        return;
    case 't':
        /* 't' */
        timerUpdateFunc();
        return;
    case 'x':
        /* 'x' */
        if (playerActor) {
            updateActorPositionMaybe(playerActor, (short)(playerActor->xPosMaybe + 2), playerActor->yPosMaybe, playerActor->isInAir);
            return;
        }
        break;
    case 'y':
        /* 'y' */
        if (playerActor) {
            updateActorPositionMaybe(playerActor, playerActor->xPosMaybe, (short)(playerActor->yPosMaybe + 2), playerActor->isInAir);
            return;
        }
    }
    return;
}

RECT* updateActorSpriteRect(Actor* actor) {
    ski_assert(actor, 931);
    ski_assert((actor->flags & FLAG_4) == 0, 932);
    ski_assert(actor->spriteIdx2 != 0, 933);

    if (&sprites[actor->spriteIdx2] != actor->spritePtr) {
        assertFailed(sourceFilename, 934);
    }
    updateRectForSpriteAtLocation(&actor->someRect, actor->spritePtr, actor->xPosMaybe, actor->yPosMaybe, actor->isInAir);
    actor->flags |= FLAG_4;
    return &actor->someRect;
}

void mainWindowPaint(HWND param_1) {
    RECT r;

    r.left = 0;
    r.right = windowClientRect.right;
    r.top = 0;
    r.bottom = windowClientRect.bottom;

    SDL_RenderClear(renderer);

    paintActors(NULL, &r);

    // draw status window
    SDL_Rect dstrect;
    dstrect.x = windowWidth - statusWindowTotalTextWidth;
    dstrect.y = 0;
    dstrect.w = statusWindowTotalTextWidth;
    dstrect.h = statusWindowHeight;
    SDL_RenderCopy(renderer, statusWindowTexture, NULL, &dstrect);

    SDL_RenderPresent(renderer);
}

void paintActors(HDC hdc, RECT* paintRect) {
    Actor* actor;
    RECT* rect;

    // ski_assert(hdc != NULL, 1347);
    ski_assert(paintRect != NULL, 1348);

    for (actor = actorListPtr; actor != NULL; actor = actor->next) {
        if ((actor->flags & 4) == 0) {
            rect = updateActorSpriteRect(actor);
        } else {
            rect = &actor->someRect;
        }
        if (doRectsOverlap(rect, paintRect)) {
            actor->flags = actor->flags & 0xfffffffe;
        }
    }
    drawWindow(hdc, paintRect);
}

void statusWindowFindLongestTextString(HDC hdc, short* maxLength, LPCSTR textStr, int textLength) {
    // SIZE size;
    // GetTextExtentPoint32A(hdc, textStr, textLength, &size);
    // if (*maxLength < size.cx)
    // {
    //     *maxLength = (short)size.cx;
    // }

    int w, h;

    TTF_SizeUTF8(statusWindowFont, textStr, &w, &h);
    if (*maxLength < w) {
        *maxLength = w;
    }
}

// Originally called when handling WM_PAINT message
void paintStatusWindow(HWND hWnd) {
    char* str;
    int len;
    short y;

    y = 2;
    // BeginPaint(hWnd, &paint);
    // hbr = (HBRUSH)GetStockObject(4);
    // FrameRect(paint.hdc, &statusBorderRect, hbr);

    SDL_LockTextureToSurface(statusWindowTexture, NULL, &statusWindowSurface);

    SDL_FillRect(statusWindowSurface, NULL, SDL_MapRGB(statusWindowSurface->format, 0, 0, 0));
    SDL_Rect r = { 1, 1, statusWindowSurface->w - 2, statusWindowSurface->h - 2 };
    SDL_FillRect(statusWindowSurface, &r, SDL_MapRGB(statusWindowSurface->format, 255, 255, 255));

    str = getCachedString(IDS_TIME);
    len = strlen(str);
    drawText(NULL, str, 2, &y, len);

    str = getCachedString(IDS_DIST);
    len = strlen(str);
    drawText(NULL, str, 2, &y, len);

    str = getCachedString(IDS_SPEED);
    len = strlen(str);
    drawText(NULL, str, 2, &y, len);

    str = getCachedString(IDS_STYLE);
    len = strlen(str);
    drawText(NULL, str, 2, &y, len);
    formatAndPrintStatusStrings(NULL);
    // EndPaint(hWnd, &paint);

    SDL_UnlockTexture(statusWindowTexture);
}

BOOL calculateStatusWindowDimensions(HWND hWnd) {
    char* str;
    int len;
    short maxKeyLength;
    short maxValueLength;
    // TEXTMETRIC textMetric;
    int w, h;

    maxKeyLength = 0;
    maxValueLength = 0;
    // statusWindowDC = GetDC(hWnd);
    // if (statusWindowDC == NULL)
    // {
    //     return 0;
    // }
    // statusWindowFont = GetStockObject(OEM_FIXED_FONT);
    // if (statusWindowFont != NULL)
    // {
    //     statusWindowFont = SelectObject(statusWindowDC, statusWindowFont);
    // }
    embedded_resource_t* res = get_embedded_resource_by_name("resources/vgaoem.fon");
    SDL_RWops* src = SDL_RWFromConstMem(res->content, res->len);
    statusWindowFont = TTF_OpenFontRW(src, 1, 12);
    if (statusWindowFont == NULL) {
        exit(1);
    }

    // GetTextMetricsA(statusWindowDC, &textMetric);
    TTF_SizeUTF8(statusWindowFont, "Ay", &w, &h);
    textLineHeight = h;
    str = getCachedString(IDS_TIME);
    len = strlen(str);
    statusWindowFindLongestTextString(statusWindowDC, &maxKeyLength, str, len);
    str = getCachedString(IDS_DIST);
    len = strlen(str);
    statusWindowFindLongestTextString(statusWindowDC, &maxKeyLength, str, len);
    str = getCachedString(IDS_SPEED);
    len = strlen(str);
    statusWindowFindLongestTextString(statusWindowDC, &maxKeyLength, str, len);
    str = getCachedString(IDS_STYLE);
    len = strlen(str);
    statusWindowFindLongestTextString(statusWindowDC, &maxKeyLength, str, len);
    str = getCachedString(IDS_TIME_BLANK);
    len = strlen(str);
    statusWindowFindLongestTextString(statusWindowDC, &maxValueLength, str, len);
    str = getCachedString(IDS_DIST_BLANK);
    len = strlen(str);
    statusWindowFindLongestTextString(statusWindowDC, &maxValueLength, str, len);
    str = getCachedString(IDS_SPEED_BLANK);
    len = strlen(str);
    statusWindowFindLongestTextString(statusWindowDC, &maxValueLength, str, len);
    str = getCachedString(IDS_STYLE_BLANK);
    len = strlen(str);
    statusWindowFindLongestTextString(statusWindowDC, &maxValueLength, str, len);
    statusWindowHeight = textLineHeight * 4;
    statusWindowTotalTextWidth = maxValueLength + maxKeyLength;
    statusWindowLabelWidth = maxKeyLength;

    // added in sdl port
    statusWindowTotalTextWidth += 5;
    statusWindowHeight += 5;

    return 1;
}

void setupActorList() {
    uint32_t uVar1;
    uint32_t uVar2;
    uint32_t uVar3;

    uVar3 = 0;
    actorListPtr = NULL;
    currentFreeActor = actors;
    uVar2 = 1;
    uVar1 = 0;
    do {
        uVar3 = uVar3 + 1;
        actors[uVar1].next = actors + uVar2;
        uVar1 = uVar3 & 0xffff;
        uVar2 = uVar1 + 1;
    } while (uVar2 < NUM_ACTORS);
    actors[uVar3].next = NULL;
}

void resetPermObjectCount() {
    permObjectCount = 0;
}

BOOL setupGame() {
    Actor* actor;
    short newY;
    short inAir;

    inAir = 0;
    newY = 0;
    actor = addActorOfType(ACTOR_TYPE_0_PLAYER, 3);
    playerActorPtrMaybe_1 = updateActorPositionMaybe(actor, 0, newY, inAir);
    playerActor = playerActorPtrMaybe_1;
    if (!playerActorPtrMaybe_1) {
        return FALSE;
    }
    setupGameTitleActors();
    setupPermObjects();
    isPaused = FALSE;
    startGameTimer();
    return TRUE;
}

void setPointerToNull(PermObjectList* param_1) {
    ski_assert(param_1, 2578);
    param_1->startingObject = NULL;
}

// LRESULT CALLBACK skiMainWndProc(HWND hWnd, uint32_t message, WPARAM wParam, LPARAM lParam)
// {
//     BOOL BVar1;
//     LRESULT LVar2;

//     if (message < 0x25)
//     {
//         switch (message)
//         {
//         case 1:
//             /* WM_CREATE */
//             BVar1 = loadBitmaps(hWnd);
//             if (BVar1 != 0)
//             {
//                 updateWindowSize(hWnd);
//                 return 0;
//             }
//             return -1;
//         case 2:
//             /* WM_MOVE */
//             handleWindowMoveMessage(hWnd);
//             PostQuitMessage(0);
//             return 0;
//         case 3:
//         case 4:
//         case 7:
//         case 8:
//         case 9:
//         case 10:
//         case 0xb:
//         case 0xc:
//         case 0xd:
//         case 0xe:
//         case 0x10:
//         case 0x11:
//         case 0x12:
//         case 0x13:
//         case 0x14:
//         case 0x15:
//         case 0x16:
//         case 0x17:
//         case 0x18:
//         case 0x19:
//         case 0x1a:
//         case 0x1b:
//         case 0x1c:
//         case 0x1d:
//         case 0x1e:
//         case 0x1f:
//         case 0x20:
//             LVar2 = DefWindowProcA(hWnd, message, wParam, lParam);
//             return LVar2;
//         case 5:
//             /* WM_SIZE */
//             updateWindowSize(hWnd);
//             if (hSkiStatusWnd != 0)
//             {
//                 handleWindowSizeMessage();
//             }
//             isMinimised = (BOOL)(wParam == 1);
//             updateWindowsActiveStatus();
//             if (inputEnabled != 0)
//             {
//                 UpdateWindow(hSkiMainWnd);
//                 return 0;
//             }
//             break;
//         case 6:
//             /* WM_ACTIVATE */
//             mainWndActivationFlags = wParam;
//             if (wParam != 0)
//             {
//                 SetFocus(hWnd);
//             }
//             updateWindowsActiveStatus();
//             return 0;
//         case 0xf:
//             /* WM_PAINT */
//             mainWindowPaint(hWnd);
//             return 0;
//         case 0x21:
//             /* WM_MOUSEACTIVATE */
//             if ((short)lParam == 1)
//             {
//                 return 2;
//             }
//             break;
//         default:
//             // TODO what is this doing?!
//             *(int *)(lParam + 0x18) = 0x140;
//             *(int *)(lParam + 0x1c) = 300;
//             return 0;
//         }
//     }
//     else if (message < WM_MOUSEMOVE + 1)
//     {
//         if (message == WM_MOUSEMOVE)
//         {
//             if (inputEnabled != 0)
//             {
//                 handleMouseMoveMessage((short)lParam, (short)((uint32_t)lParam >> 0x10));
//                 return 0;
//             }
//         }
//         else if (message == WM_KEYDOWN)
//         {
//             if (inputEnabled != 0)
//             {
//                 handleKeydownMessage(wParam);
//                 return 0;
//             }
//         }
//         else
//         {
//             if (message != WM_CHAR)
//             {
//                 LVar2 = DefWindowProcA(hWnd, message, wParam, lParam);
//                 return LVar2;
//             }
//             /* WM_CHAR */
//             if (inputEnabled != 0)
//             {
//                 handleCharMessage(wParam);
//                 return 0;
//             }
//         }
//     }
//     else
//     {
//         if ((message != WM_LBUTTONDOWN) && (message != WM_LBUTTONDBLCLK))
//         {
//             LVar2 = DefWindowProcA(hWnd, message, wParam, lParam);
//             return LVar2;
//         }
//         if (inputEnabled != 0)
//         {
//             handleMouseClick();
//         }
//     }
//     return 0;
// }

void updateWindowsActiveStatus() {
    if ((mainWndActivationFlags != 0) && (isMinimised == 0)) {
        inputEnabled = 1;
        startGameTimer();
        return;
    }
    inputEnabled = 0;
    pauseGame();
}

BOOL loadBitmaps(HWND hWnd) {
    // mainWindowDC = GetDC(hWnd);
    // if (!mainWindowDC)
    // {
    //     return FALSE;
    // }
    smallBitmapDC = NULL;
    smallBitmapDC_1bpp = NULL;
    largeBitmapDC = NULL;
    largeBitmapDC_1bpp = NULL;
    bitmapSourceDC = NULL;
    smallBitmapSheet = NULL;
    smallBitmapSheet_1bpp = NULL;
    largeBitmapSheet = NULL;
    largeBitmapSheet_1bpp = NULL;
    scratchBitmap = NULL;
    if (!createBitmapSheets(mainWindowDC)) {
        showErrorMessage("Whoa, like, can't load bitmaps!  Yer outa memory, duuude!");
        return FALSE;
    }
    return TRUE;
}

HBITMAP loadBitmapResource(uint32_t resourceId) {
    char filename[256];

    // return LoadBitmapA(skiFreeHInstance, MAKEINTRESOURCE(resourceId));

    sprintf(filename, "resources/ski32_%d.bmp", resourceId);
    // SDL_Surface* bitmap = IMG_Load(filename);

    embedded_resource_t* res = get_embedded_resource_by_name(filename);
    SDL_RWops* src = SDL_RWFromConstMem(res->content, res->len);
    SDL_Surface* bitmap = IMG_Load_RW(src, 1);
    return bitmap;
}

void handleWindowMoveMessage(HWND hWnd) {
    // ReleaseDC(hWnd, mainWindowDC);
    pauseGame();
    deleteWindowObjects();
}

void statusWindowReleaseDC(HWND hWnd) {
    // if (hWnd != hSkiStatusWnd)
    // {
    //     assertFailed(sourceFilename, 4387);
    // }
    // if (statusWindowFont)
    // {
    //     SelectObject(statusWindowDC, statusWindowFont);
    // }
    // ReleaseDC(hWnd, statusWindowDC);
}

void actorSetFlag8IfFlag1IsUnset(Actor* actor) {
    ski_assert(actor, 865);

    if ((actor->flags & FLAG_1) == 0) {
        if (actor->linkedActor) {
            actor->linkedActor->linkedActor = NULL;
        }
        actor->flags |= FLAG_8;
    }
}

void removeFlag8ActorsFromList() {
    Actor* currentActor;
    Actor* prevActor;

    currentActor = actorListPtr;
    prevActor = (Actor*)&actorListPtr;
    if (actorListPtr) {
        do {
            if ((currentActor->flags & FLAG_8) != 0) {
                if (currentActor->permObject) {
                    ski_assert(currentActor->permObject->actor == currentActor, 886);
                    currentActor->permObject->actor = NULL;
                }
                if (currentActor == playerActor) {
                    playerActor = NULL;
                }
                if (currentActor == playerActorPtrMaybe_1) {
                    playerActorPtrMaybe_1 = NULL;
                }
                prevActor->next = currentActor->next;
                currentActor->next = currentFreeActor;
                currentFreeActor = currentActor;
            } else {
                prevActor = currentActor;
            }
            currentActor = prevActor->next;
        } while (currentActor != NULL);
    }
}

BOOL changeScratchBitmapSize(short newWidth, short newHeight) {
    // HGDIOBJ ho;
    // HBITMAP h;

    if ((newWidth > scratchBitmapWidth) || (newHeight) > scratchBitmapHeight) {
        scratchBitmapWidth = (newWidth & 0xffc0) + 0x40;
        scratchBitmapHeight = (newHeight & 0xffc0) + 0x40;
        if (scratchBitmap != NULL) {
            SDL_FreeSurface(scratchBitmap);
            scratchBitmap = NULL;
        }
        // h = CreateCompatibleBitmap(mainWindowDC, scratchBitmapWidth, scratchBitmapHeight);
        scratchBitmap = SDL_CreateRGBSurface(0, scratchBitmapWidth, scratchBitmapHeight, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000);
    }
    //     while (h == (HBITMAP)0x0)
    //     {
    //         if (scratchBitmapWidth == newWidth && scratchBitmapHeight == newHeight)
    //         {
    //             scratchBitmapWidth = 0;
    //             scratchBitmapHeight = 0;
    //             return FALSE;
    //         }
    //         scratchBitmapWidth = newWidth;
    //         scratchBitmapHeight = newHeight;
    //         h = CreateCompatibleBitmap(mainWindowDC, newWidth, newHeight);
    //     }
    //     scratchBitmap = SelectObject(bitmapSourceDC, h);
    // }
    return TRUE;
}

void actorClearFlag10(Actor* actor1, Actor* linkedActor) {
    Actor* pAVar1;
    Actor* pAVar2 = actor1;
    Actor** ppAVar3;

    ski_assert(actor1, 1252);
    ski_assert(linkedActor, 1253);
    ski_assert((actor1->flags & FLAG_10), 1254); // <---
    ski_assert((linkedActor->flags & FLAG_10), 1255);
    ski_assert(actor1 != linkedActor, 1256);

    ppAVar3 = &actor1->actorPtr;
    pAVar1 = actor1->actorPtr;

    while (pAVar1 != NULL) {
        pAVar2 = *ppAVar3;
        if ((pAVar2->flags & FLAG_10) != 0) {
            assertFailed(sourceFilename, 1260);
        }
        ppAVar3 = &pAVar2->actorPtr;
        pAVar1 = pAVar2->actorPtr;
    }
    pAVar2->actorPtr = linkedActor;

    enlargeRect(&actor1->rect, &linkedActor->rect);
    /* clear FLAG_10 */
    linkedActor->flags &= 0xffffffef;
}

Actor* setActorFrameNo(Actor* actor, uint32_t ActorframeNo) {
    Actor* pAVar1;

    ski_assert(actor, 1084);
    ski_assert((int)ActorframeNo < 64, 1085);

    if (actor->frameNo != ActorframeNo) {
        ski_assert(ActorframeNo < 64, 1088);

        pAVar1 = actorSetSpriteIdx(actor, actorFrameToSpriteTbl[ActorframeNo]);
        pAVar1->frameNo = ActorframeNo;
        return pAVar1;
    }
    return actor;
}

BOOL isSlowTile(short spriteIdx) {
    if ((spriteIdx != 27) && (spriteIdx != 82)) {
        return FALSE;
    }
    return TRUE;
}

Actor* actorSetSpriteIdx(Actor* actor, uint16_t spriteIdx) {
    ski_assert(actor, 979);
    if (spriteIdx != actor->spriteIdx2) {
        totalAreaOfActorSprites = totalAreaOfActorSprites - actor->spritePtr->totalPixels;
        if ((actor->flags & FLAG_1) != 0) {
            actor = duplicateAndLinkActor(actor);
        }
        actor->spriteIdx2 = spriteIdx;
        actor->spritePtr = &sprites[spriteIdx];
        totalAreaOfActorSprites = totalAreaOfActorSprites + actor->spritePtr->totalPixels;
        actor->flags = actor->flags & (int)0xfffffffb;
        actor->flags |= FLAG_20;

        actor->flags = ((isSlowTile(spriteIdx) & 1) << 6) | (actor->flags & 0xffffffbf);
    }
    return actor;
}

Actor* duplicateAndLinkActor(Actor* actor) {
    Actor* pAVar1;

    ski_assert(actor, 947);
    ski_assert((actor->flags & FLAG_1), 949);

    pAVar1 = addActor(actor, 1);
    actor->linkedActor = pAVar1;
    if (pAVar1 != NULL) {
        pAVar1->linkedActor = actor;
        pAVar1->flags |= FLAG_2;
        actor->flags &= 0xfffffffe; // Clear FLAG_1
    }
    return actor;
}

Actor* updateActorWithOffscreenStartingPosition(Actor* actor, int borderType) {
    short y;
    short x;

    if (actor) {
        getRandomOffscreenStartingPosition(borderType, &x, &y);
        actor = updateActorPositionMaybe(actor, x, y, 0);
    }

    return actor;
}

// TODO this function isn't byte perfect with the original
Actor* updateActorPositionMaybe(Actor* actor, short newX, short newY, short inAir) {
    BOOL hasMoved;
    BOOL bVar3;
    BOOL bVar4;
    BOOL isPlayer;
    uint32_t flags;

    if ((actor->xPosMaybe == newX) && (actor->yPosMaybe == newY)) {
        hasMoved = FALSE;
    } else {
        hasMoved = TRUE;
    }
    bVar4 = actor->isInAir != inAir;
    isPlayer = actor == playerActorPtrMaybe_1;
    ski_assert(actor, 1037);

    if (isPlayer && hasMoved) {
        updateActorRectsAfterPlayerMove(newX, newY);
    }

    if (hasMoved || bVar4) {
        flags = actor->flags;
        if ((flags & FLAG_1) != 0) {
            actor = duplicateAndLinkActor(actor);
        }
        if ((flags & FLAG_4) == 0 || !isPlayer || bVar4) {
            bVar3 = 0;
        } else {
            bVar3 = 1;
        }
        actor->yPosMaybe = newY;
        actor->xPosMaybe = newX;
        actor->isInAir = inAir;
        actor->flags = (uint32_t)(bVar3 | 8) << 2 | (actor->flags & 0xfffffffb);
    }
    return actor;
}

// TODO not byte accurate
void updateActorRectsAfterPlayerMove(short newPlayerX, short newPlayerY) {
    short dx = newPlayerX - playerX;
    short dy = newPlayerY - playerY;
    Actor* actor = actorListPtr;

    for (; actor != NULL; actor = actor->next) {
        if (actor != playerActorPtrMaybe_1 && (actor->flags & FLAG_4) != 0 && (actor->flags & FLAG_2) == 0) {
            if ((actor->flags & FLAG_1) != 0) {
                duplicateAndLinkActor(actor);
            }

            actor->someRect.left -= dx;
            actor->someRect.right -= dx;
            actor->someRect.top -= dy;
            actor->someRect.bottom -= dy;
        }
    }

    playerX = newPlayerX;
    playerY = newPlayerY;
}

void getRandomOffscreenStartingPosition(int borderType, short* xPos, short* yPos) {
    short sVar1;

    *xPos = (short)playerX - (short)skierScreenXOffset;
    *yPos = playerY - (short)skierScreenYOffset;
    switch (borderType) {
    case BORDER_LEFT:
    case BORDER_RIGHT:
        if (borderType == BORDER_LEFT) {
            sVar1 = (short)windowClientRect.left + -0x3c;
        } else {
            sVar1 = (short)windowClientRect.right + 0x3c;
        }
        *xPos = *xPos + sVar1;
        *yPos = *yPos + (short)windowClientRect.top + ski_random(windowHeight);
        return;
    case BORDER_TOP:
    case BORDER_BOTTOM:
        break;
    default:
        assertFailed(sourceFilename, 1454);
        return;
    }
    *xPos = *xPos + ski_random(windowWidth) + (short)windowClientRect.left;
    if (borderType == BORDER_TOP) {
        *yPos = *yPos + (short)windowClientRect.top + -0x3c;
        return;
    }
    *yPos = *yPos + (short)windowClientRect.bottom + 0x3c;
    return;
}

Actor* addRandomActor(int borderType) {
    uint16_t spriteIdx;
    int actorType;
    Actor* actor = NULL;
    short y;
    short x;

    getRandomOffscreenStartingPosition(borderType, &x, &y);
    if ((((x < -576) || (-320 < x)) || (y < 640)) || (8640 < y)) {
        if (((x < 320) || (512 < x)) || ((y < 640 || (16640 < y)))) {
            if (((x < -160) || (160 < x)) || ((y < 640 || (16640 < y)))) {
                actorType = randomActorType1();
            } else {
                actorType = randomActorType2();
            }
        } else {
            actorType = randomActorType3();
        }
    } else {
        actorType = areaBasedActorType();
    }
    // actorType = 11;
    if (actorType != 0x12) {
        if (actorType < 0xb) {
            actor = addActorOfType(actorType, uint32_t_ARRAY_0040a22c[actorType]);
        } else {
            spriteIdx = getSpriteIdxForActorType(actorType);
            actor = addActorOfTypeWithSpriteIdx(actorType, spriteIdx);
        }
        if (actor != NULL) {
            actor = updateActorPositionMaybe(actor, x, y, 0);
            return actor;
        }
    }

    return actor;
}

int randomActorType1(void) {
    uint16_t uVar1;

    if (totalAreaOfActorSprites > windowWithMarginTotalArea / 32) {
        return 0x12;
    }

    uVar1 = ski_random(1000);
    if (uVar1 < 0x32) {
        return 10;
    }
    if (uVar1 < 500) {
        return ACTOR_TYPE_13_TREE;
    }
    if (uVar1 < 700) {
        return ACTOR_TYPE_15_BUMP;
    }
    if (uVar1 < 0x2ee) {
        return ACTOR_TYPE_11_MOGULS;
    }
    if (uVar1 < 0x3b6) {
        return ACTOR_TYPE_14_ROCK_STUMP;
    }
    if (uVar1 < 0x3ca) {
        return ACTOR_TYPE_16_JUMP;
    }
    return (uVar1 < 0x3de) ? 1 : 2;
}

int areaBasedActorType() {
    return (totalAreaOfActorSprites > windowWithMarginTotalArea / 64) ? ACTOR_TYPE_18_NOTHING : ACTOR_TYPE_11_MOGULS;
}

int randomActorType3() {
    if (totalAreaOfActorSprites > windowWithMarginTotalArea / 16) {
        return ACTOR_TYPE_18_NOTHING;
    }

    return ski_random(0x40) != 0 ? ACTOR_TYPE_13_TREE : ACTOR_TYPE_2_DOG;
}

int randomActorType2() {
    uint16_t uVar1;

    if (totalAreaOfActorSprites > windowWithMarginTotalArea / 32) {
        return 0x12;
    }

    uVar1 = ski_random(100);
    if (uVar1 < 2) {
        return 0xa;
    }
    if (uVar1 < 0x14) {
        return ACTOR_TYPE_13_TREE;
    }
    if (uVar1 < 0x32) {
        return ACTOR_TYPE_15_BUMP;
    }
    if (uVar1 < 0x3c) {
        return ACTOR_TYPE_11_MOGULS;
    }
    return uVar1 < 0x50 ? ACTOR_TYPE_14_ROCK_STUMP : ACTOR_TYPE_16_JUMP;
}

Actor* updateActor(Actor* actor) {
    ski_assert(actor, 2311);
    ski_assert(actor->typeMaybe < 11 && !actor->permObject, 2312);

    switch (actor->typeMaybe) {
    case ACTOR_TYPE_0_PLAYER:
        return updatePlayerActor(actor);
    case ACTOR_TYPE_3_SNOWBOARDER:
        return updateActorType3_snowboarder(actor);
    case ACTOR_TYPE_2_DOG:
        return updateActorType2_dog(actor);
    case ACTOR_TYPE_1_BEGINNER:
        return updateActorType1_Beginner(actor);
    default:
        assertFailed(sourceFilename, 2335);
        return actor;
    case ACTOR_TYPE_9_TREE_ON_FIRE:
        return updateActorType9_treeOnFire(actor);
    case ACTOR_TYPE_10_WALKING_TREE:
        return updateActorTypeA_walkingTree(actor);
    }
}

// TODO not 100% byte accurate. missing assert and some other logic differences
Actor* updatePlayerActor(Actor* actor) {
    short sVar1;
    short uVar5;
    Actor* pAVar2;
    Sound* sound;
    int points;
    uint32_t ActorframeNo;
    short xPos;
    short yPos;

    xPos = actor->xPosMaybe;
    yPos = actor->yPosMaybe;
    ActorframeNo = actor->frameNo;
    ski_assert(actor, 2022);
    ski_assert(actor->typeMaybe == 0, 2023);

    if (ActorframeNo == 0xb) {
        ski_assert(actor->isInAir == 0, 2027);
        ski_assert(actor->inAirCounter == 0, 2028);

        sVar1 = actor->HorizontalVelMaybe;
        if ((sVar1 == 0) && (actor->verticalVelocityMaybe == 0)) {
            ActorframeNo = 0xc;
        }
        if (sVar1 < 0) {
            uVar5 = -1;
        } else {
            uVar5 = (short)(sVar1 > 0);
        }
        actor->HorizontalVelMaybe = sVar1 - uVar5;
        sVar1 = actor->verticalVelocityMaybe;
        if (sVar1 < 0) {
            actor->verticalVelocityMaybe = sVar1 + 1;
        } else {
            actor->verticalVelocityMaybe = (sVar1 > 0) ? sVar1 - 1 : sVar1;
        }
    } else {
        pAVar2 = updateActorPositionWithVelocityMaybe(actor);
        ski_assert(ActorframeNo < 22, 2040);

        actor = updateActorVelMaybe(pAVar2, &ActorVelStruct_ARRAY_0040a308[ActorframeNo]);
        switch (ActorframeNo) {
        case 8:
        case 10:
            ActorframeNo = 6;
            break;
        case 7:
        case 9:
            ActorframeNo = 3;
            break;
        case 0xd:
        case 0xe:
        case 0xf:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
            if (actor->isInAir == 0) {
                ski_assert(ActorframeNo - 0xd < 9, 2066);

                ActorframeNo = uint32_t_ARRAY_0040a434[ActorframeNo];
                if (ActorframeNo == 0x11) {
                    addStylePoints(-0x40);
                    sound = &sound_1;
                } else {
                    sound = &sound_4;
                }
                playSound(sound);
            }
        }
    }
    pAVar2 = setActorFrameNo(actor, ActorframeNo);
    switch (ActorframeNo) {
    case 7:
    case 8:
    case 9:
    case 10:
        points = -1;
        break;
    default:
        goto switchD_00402b0b_caseD_b;
    case 0x14:
    case 0x15:
        points = 8;
        break;
    case 0x12:
    case 0x13:
        points = 4;
        break;
    case 0x10:
        points = 2;
        break;
    }
    addStylePoints(points);
switchD_00402b0b_caseD_b:
    updateSsGameMode(pAVar2, xPos, yPos);
    updateFsGameMode(pAVar2, xPos, yPos);
    updateGsGameMode(pAVar2, xPos, yPos);
    return pAVar2;
}

// TODO this isn't byte compatible.
void updateSsGameMode(Actor* actor, short xPos, short yPos) {
    int iVar1;
    uint16_t spriteIdx;
    short x;
    short y;

    if (actor == playerActor) {
        x = actor->xPosMaybe;
        y = actor->yPosMaybe;
        ski_assert(actor->typeMaybe == 0, 1788);
        if (isSsGameMode != 0) {
            elapsedTime = currentTickCount - timedGameRelated;
            if (y > 8640) {
                iVar1 = FUN_00402e30(currentTickCount, prevTickCount, (int)y, (int)yPos, 0x21c0);
                isSsGameMode = 0;
                elapsedTime = iVar1 - timedGameRelated;
                INT_0040c964 = 1;
                resetPlayerFrameNo();
                updateEntPackIniKeyValue(iniSsConfigKey, elapsedTime, 1); // TODO this is currently a jmp when it should be a call + ret
                return;
            }
            if (y <= 0x280) {
                isSsGameMode = 0;
                return;
            }
            if (y > currentSlalomFlag->maybeY) {
                spriteIdx = 0x19;
                iVar1 = FUN_00402e30((int)x, (int)xPos, (int)y, (int)yPos, (int)currentSlalomFlag->maybeY);
                if (((currentSlalomFlag->spriteIdx == 0x17) && ((short)iVar1 > currentSlalomFlag->maybeX)) || ((currentSlalomFlag->spriteIdx == 0x18 && ((short)iVar1 < currentSlalomFlag->maybeX)))) {
                    spriteIdx = 0x1a;
                    timedGameRelated = timedGameRelated - 5000;
                }
                permObjectSetSpriteIdx(currentSlalomFlag, spriteIdx);
                currentSlalomFlag = currentSlalomFlag + 1;
                return;
            }
        } else {
            if ((yPos <= 0x280) && (y > 0x280)) {
                iVar1 = FUN_00402e30((int)x, (int)xPos, (int)y, (int)yPos, 0x280);
                if (((short)iVar1 >= -576) && ((short)iVar1 <= -320)) {
                    isSsGameMode = 1;
                    timedGameRelated = FUN_00402e30(currentTickCount, prevTickCount, (int)y, (int)yPos, 0x280);
                    elapsedTime = timedGameRelated - currentTickCount;
                    currentSlalomFlag = firstSlalomFlagLeft;
                }
            }
        }
    }
}

int FUN_00402e30(int param_1, int param_2, int param_3, int param_4, int param_5) {
    ski_assert(param_3 != param_4, 1612);
    return param_1 - ((param_1 - param_2) * (param_3 - param_5)) / (param_3 - param_4);
}

void resetPlayerFrameNo() {
    uint32_t ActorframeNo;

    if (playerActor) {
        ActorframeNo = playerActor->frameNo;
        if ((ActorframeNo != 0xb) && (ActorframeNo != 0x11)) {
            ActorframeNo = (playerActor->isInAir > 0) ? 0xe : 3;
        }
        setActorFrameNo(playerActor, ActorframeNo);
        formatAndPrintStatusStrings(statusWindowDC);
    }
}

// TODO not byte compatible with the original
void updateEntPackIniKeyValue(LPCSTR configKey, int value, int isTime) {
    // char cVar1;
    // int *valuePtr;
    // uint32_t uVar2;
    // int iVar3;
    // uint16_t uVar4 = 0;
    // uint16_t yourscoreIdx;
    // char *pcVar5;
    // LPSTR outputString;
    // uint16_t uVar6;
    // char *bufPtr;
    // int hiScoreTbl[10];
    // char lineBuf[256];

    //     bufPtr = lineBuf;

    //     if (isTime != 0)
    //     {
    //         value = -value;
    //     }
    //     GetPrivateProfileStringA(entpack_ini_section_name_ski, configKey, (LPCSTR)&statusWindowNameStrPtr, lineBuf, 0x100,
    //                              s_entpack_ini);
    //     while ((bufPtr[0] != '\0' && (uVar4 < 10)))
    //     {
    //         cVar1 = *bufPtr;
    //         while (cVar1 == ' ')
    //         {
    //             pcVar5 = bufPtr + 1;
    //             bufPtr = bufPtr + 1;
    //             cVar1 = *pcVar5;
    //         }
    //         cVar1 = *bufPtr;
    //         pcVar5 = bufPtr;
    //         if (cVar1 != ' ')
    //         {
    //             do
    //             {
    //                 if (cVar1 == '\0')
    //                     break;
    //                 cVar1 = pcVar5[1];
    //                 pcVar5 = pcVar5 + 1;
    //             } while (cVar1 != ' ');
    //             if (pcVar5 != bufPtr)
    //             {
    //                 if (*pcVar5 != '\0')
    //                 {
    //                     *pcVar5 = '\0';
    //                     pcVar5 = pcVar5 + 1;
    //                 }
    //                 hiScoreTbl[uVar4] = atol(bufPtr);
    //                 uVar4++;
    //                 bufPtr = pcVar5;
    //             }
    //         }
    //         //        lineBuf[0] = *bufPtr;
    //     }
    //     yourscoreIdx = 0;
    //     if (uVar4 > 0)
    //     {
    //         do
    //         {
    //             if (value > hiScoreTbl[yourscoreIdx])
    //                 break;
    //             yourscoreIdx = yourscoreIdx + 1;
    //         } while (yourscoreIdx < uVar4);
    //         if (yourscoreIdx >= 10)
    //             goto LAB_00402fe2;
    //     }
    //     if (uVar4 == 10)
    //     {
    //         uVar4 = 9;
    //     }
    //     uVar6 = uVar4;
    //     if (uVar4 > yourscoreIdx)
    //     {
    //         valuePtr = hiScoreTbl + uVar4;
    //         iVar3 = (uint32_t)uVar4 - (uint32_t)yourscoreIdx;
    //         do
    //         {
    //             uVar6 = uVar6 - 1;
    //             *valuePtr = valuePtr[-1];
    //             valuePtr--;
    //             iVar3 = iVar3 + -1;
    //         } while (iVar3 != 0);
    //     }
    //     uVar4 = uVar4 + 1;
    //     hiScoreTbl[uVar6] = value;
    // LAB_00402fe2:
    //     bufPtr = lineBuf;
    //     if (uVar4 > 0)
    //     {
    //         valuePtr = hiScoreTbl;
    //         uVar2 = (uint32_t)uVar4;
    //         do
    //         {
    //             iVar3 = wsprintfA(bufPtr, scoreFormatString, *valuePtr);
    //             bufPtr = bufPtr + iVar3;
    //             valuePtr = valuePtr + 1;
    //             uVar2 = uVar2 - 1;
    //         } while (uVar2 != 0);
    //     }
    //     WritePrivateProfileStringA(entpack_ini_section_name_ski, configKey, lineBuf, s_entpack_ini);
    //     uVar6 = 0;
    //     bufPtr = lineBuf;
    //     if (uVar4 > 0)
    //     {
    //         for (valuePtr = hiScoreTbl; uVar6 < uVar4; uVar6++, valuePtr++)
    //         {
    //             if (uVar6 > 0)
    //             {
    //                 *bufPtr = '\n';
    //                 bufPtr++;
    //             }
    //             if (isTime != 0)
    //             {
    //                 uVar2 = formatElapsedTime(-*valuePtr, bufPtr);
    //                 uVar2 = uVar2 & 0xffff;
    //             }
    //             else
    //             {
    //                 uVar2 = wsprintfA(bufPtr, stylePointsFormatString, *valuePtr);
    //             }
    //             bufPtr = bufPtr + uVar2;
    //             if (uVar6 == yourscoreIdx)
    //             {
    //                 pcVar5 = getCachedString(IDS_THATS_YOU);
    //                 uVar2 = wsprintfA(bufPtr, scoreStringFormatString, pcVar5);
    //                 bufPtr = bufPtr + uVar2;
    //             }
    //         }
    //         //        valuePtr = hiScoreTbl;
    //         //        do {
    //         //            if (uVar6 > 0) {
    //         //                *bufPtr = '\n';
    //         //                bufPtr++;
    //         //            }
    //         //            if (isTime != 0) {
    //         //                uVar2 = formatElapsedTime(-*valuePtr,bufPtr);
    //         //                uVar2 = uVar2 & 0xffff;
    //         //            }
    //         //            else {
    //         //                uVar2 = wsprintfA(bufPtr,stylePointsFormatString,*valuePtr);
    //         //            }
    //         //            bufPtr = bufPtr + uVar2;
    //         //            if (uVar6 == yourscoreIdx) {
    //         //                pcVar5 = getCachedString(IDS_THATS_YOU);
    //         //                uVar2 = wsprintfA(bufPtr,scoreStringFormatString,pcVar5);
    //         //                bufPtr = bufPtr + uVar2;
    //         //            }
    //         //            uVar6++;
    //         //            valuePtr++;
    //         //        } while (uVar6 < uVar4);
    //     }
    //     if (yourscoreIdx == 10)
    //     {
    //         iVar3 = wsprintfA(bufPtr, newlineString);
    //         outputString = bufPtr + iVar3;
    //         //        outputString = bufPtr + wsprintfA(bufPtr,newlineString);
    //         if (isTime != 0)
    //         {
    //             uVar2 = formatElapsedTime(-value, outputString);
    //             uVar2 = uVar2 & 0xffff;
    //         }
    //         else
    //         {
    //             uVar2 = wsprintfA(outputString, stylePointsFormatString, value);
    //         }
    //         bufPtr = getCachedString(IDS_TRY_AGAIN);
    //         wsprintfA(outputString + uVar2, scoreStringFormatString, bufPtr);
    //     }
    //     bufPtr = getCachedString(IDS_HIGH_SCORES);
    //     MessageBoxA(hSkiMainWnd, lineBuf, bufPtr, 0);
    //     return;
}

// TODO not byte compatible. jmp actorSetSpriteIdx rather than call.
void permObjectSetSpriteIdx(PermObject* permObject, uint16_t spriteIdx) {
    ski_assert(permObject, 1773);

    permObject->spriteIdx = spriteIdx;
    permObject->spritePtr = &sprites[spriteIdx];
    if (permObject->actor) {
        actorSetSpriteIdx(permObject->actor, spriteIdx);
    }
}

// TODO not byte compatible. Another jmp instead of call.
void updateFsGameMode(Actor* actor, short xPos, short yPos) {
    int iVar1;
    short x;
    short y;

    if (actor == playerActor) {
        x = actor->xPosMaybe;
        y = actor->yPosMaybe;
        ski_assert(actor->typeMaybe == 0, 1839);

        if (isFsGameMode != 0) {
            if (y > 0x4100) {
                isFsGameMode = 0;
                INT_0040c968 = 1;
                resetPlayerFrameNo();
                updateEntPackIniKeyValue(iniFsConfigKey, stylePoints, 0);
                return;
            }
            if (y <= 0x280) {
                isFsGameMode = 0;
                return;
            }
        } else {
            if ((yPos <= 0x280) && (0x280 < y)) {
                iVar1 = FUN_00402e30((int)x, (int)xPos, (int)y, (int)yPos, 0x280);
                if (((short)iVar1 >= -160) && ((short)iVar1 <= 160)) {
                    isFsGameMode = 1;
                }
            }
        }
    }
}

// TODO not byte compatible. Another jmp instead of call.
void updateGsGameMode(Actor* actor, short xPos, short yPos) {
    int iVar1;
    uint16_t spriteIdx;
    short x;
    short y;

    if (actor == playerActor) {
        x = actor->xPosMaybe;
        y = actor->yPosMaybe;
        ski_assert(actor->typeMaybe == 0, 1870);
        if (isGsGameMode != 0) {
            elapsedTime = currentTickCount - timedGameRelated;
            if (0x4100 < y) {
                iVar1 = FUN_00402e30(currentTickCount, prevTickCount, (int)y, (int)yPos, 0x4100);
                isGsGameMode = 0;
                elapsedTime = iVar1 - timedGameRelated;
                INT_0040c960 = 1;
                resetPlayerFrameNo();
                updateEntPackIniKeyValue(iniGsConfigKey, elapsedTime, 1);
                return;
            }
            if (y <= 640) {
                isGsGameMode = 0;
                return;
            }
            /* FIXME this decomp isn't right */
            if (y > currentSlalomFlag->maybeY) {
                spriteIdx = 0x19;
                iVar1 = FUN_00402e30((int)x, (int)xPos, (int)y, (int)yPos, (int)currentSlalomFlag->maybeY);
                if (((currentSlalomFlag->spriteIdx == 0x17) && ((short)iVar1 > currentSlalomFlag->maybeX)) || ((currentSlalomFlag->spriteIdx == 0x18 && ((short)iVar1 < currentSlalomFlag->maybeX)))) {
                    spriteIdx = 0x1a;
                    timedGameRelated = timedGameRelated - 5000;
                }
                permObjectSetSpriteIdx(currentSlalomFlag, spriteIdx);
                currentSlalomFlag = currentSlalomFlag + 1;
                return;
            }
        } else {
            if ((yPos <= 0x280) && (0x280 < y)) {
                iVar1 = FUN_00402e30((int)x, (int)xPos, (int)y, (int)yPos, 0x280);
                if (((short)iVar1 >= 320) && ((short)iVar1 <= 0x200)) {
                    isGsGameMode = 1;
                    timedGameRelated = FUN_00402e30(currentTickCount, prevTickCount, (int)y, (int)yPos, 0x280);
                    elapsedTime = timedGameRelated - currentTickCount;
                    currentSlalomFlag = FirstSlalomFlagRight;
                }
            }
        }
    }
}

// TODO not byte accurate.
Actor* updateActorVelMaybe(Actor* actor, const ActorVelStruct* param_2) {
    short xRelated;
    short sVar1;
    int iVar2;
    short existingHorizontalVel;
    short existingVerticalVel;

    existingHorizontalVel = actor->HorizontalVelMaybe;
    existingVerticalVel = actor->verticalVelocityMaybe;
    ski_assert(actor, 1951);

    if (param_2 == (ActorVelStruct*)0x0) {
        assertFailed(sourceFilename, 1952);
    }
    if (actor->frameNo != param_2->frameNo) {
        assertFailed(sourceFilename, 1953);
    }
    xRelated = param_2->xRelated;
    if (xRelated == 0) {
        if (existingHorizontalVel < 0) {
            xRelated = -1;
        } else {
            xRelated = (short)(0 < existingHorizontalVel);
        }
    }
    existingHorizontalVel = xRelated * existingHorizontalVel;
    if (existingVerticalVel > 0) {
        iVar2 = (int)existingVerticalVel;
    } else {
        iVar2 = 0;
    }
    sVar1 = (short)((param_2->unk_6 * iVar2) / 2);
    if (existingHorizontalVel > sVar1) {
        iVar2 = existingHorizontalVel - 2;
        if ((int)sVar1 <= iVar2) {
            //            LAB_004034f1:
            sVar1 = (short)iVar2;
        }
    } else {
        iVar2 = (int)existingHorizontalVel + (int)param_2->unk_4;
        if (iVar2 <= sVar1) { // goto LAB_004034f1;
            sVar1 = (short)iVar2;
        }
    }
    existingHorizontalVel = param_2->unk_2;
    if (existingHorizontalVel < existingVerticalVel) {
        iVar2 = existingVerticalVel + -2;
        if (existingHorizontalVel <= iVar2) { // goto LAB_0040351c;
            existingHorizontalVel = (short)iVar2;
        }
    } else {
        iVar2 = (int)param_2->unk_0 + (int)existingVerticalVel;
        if (iVar2 <= existingHorizontalVel) { // goto LAB_0040351c;
            existingHorizontalVel = (short)iVar2;
        }
    }
    //    existingHorizontalVel = (short)iVar2;
    //    LAB_0040351c:
    actor->verticalVelocityMaybe = existingHorizontalVel;
    actor->HorizontalVelMaybe = xRelated * sVar1;
    return actor;
}

// TODO not byte accurate
Actor* updateActorTypeA_walkingTree(Actor* actor) {
    uint16_t uVar2;
    Actor* pAVar3;
    int ActorframeNo;

    ActorframeNo = actor->frameNo;
    ski_assert(actor->typeMaybe == 10, 2217);
    ski_assert(ActorframeNo >= 0x3c, 2218);
    ski_assert(ActorframeNo < 0x40, 2219);

    switch (ActorframeNo) {
    case 0x3c:
        if (actor->HorizontalVelMaybe != 0) {
            assertFailed(sourceFilename, 2223);
        }
        uVar2 = ski_random(100);
        if (uVar2 == 0) {
            uVar2 = ski_random(2);
            actor->HorizontalVelMaybe = uVar2 * 2 + -1;
            pAVar3 = updateActorPositionWithVelocityMaybe(actor);
            return setActorFrameNo(pAVar3, 0x3d);
        }
        break;
    case 0x3d:
        ski_assert(actor->HorizontalVelMaybe != 0, 2232);
        uVar2 = ski_random(10);
        if (uVar2 != 0) {
            pAVar3 = updateActorPositionWithVelocityMaybe(actor);
            pAVar3 = setActorFrameNo(pAVar3, (actor->HorizontalVelMaybe >= 0) ? 0x3f : 0x3e);
            return pAVar3;
        } else {
            actor->HorizontalVelMaybe = 0;
            pAVar3 = updateActorPositionWithVelocityMaybe(actor);
            pAVar3 = setActorFrameNo(pAVar3, 0x3c);
            return pAVar3;
        }
    case 0x3e:
        ski_assert(actor->HorizontalVelMaybe < 0, 2243);
        ActorframeNo = 0x3d;
        break;
    case 0x3f:
        ski_assert(actor->HorizontalVelMaybe > 0, 2248);
        ActorframeNo = 0x3d;
        break;
    default:
        break;
    }

    pAVar3 = updateActorPositionWithVelocityMaybe(actor);
    pAVar3 = setActorFrameNo(pAVar3, ActorframeNo);
    return pAVar3;
}

// TODO not byte accurate
Actor* updateActorType3_snowboarder(Actor* actor) {
    uint32_t ActorframeNo;

    ActorframeNo = actor->frameNo;
    ski_assert(actor->typeMaybe == 3, 2274);

    actor = updateActorPositionWithVelocityMaybe(actor);
    ski_assert(ActorframeNo - 0x1f < 8, 2277);

    actor = updateActorVelMaybe(actor, &snowboarderActorMovementTbl[ActorframeNo - 0x1f]);
    if (ActorframeNo == 0x1f) {
        if (ski_random(10) == 0) {
            ActorframeNo = 0x20;
        }
    } else if (ActorframeNo == 0x20) {
        if (ski_random(10) == 0) {
            return setActorFrameNo(actor, 0x1f);
        }
    } else if (ActorframeNo == 0x21) {
        if (actor->isInAir == 0) {
            return setActorFrameNo(actor, 0x20);
        }
    } else {
        ski_assert(((int)ActorframeNo >= 0x22) && ((int)ActorframeNo < 0x27), 2298);

        ActorframeNo++;
        if (ActorframeNo == 0x27) {
            return setActorFrameNo(actor, 0x20);
        }
    }
    return setActorFrameNo(actor, ActorframeNo);
}

// TODO not byte accurate
Actor* handleActorCollision(Actor* actor1, Actor* actor2) {
    short sVar1;
    short sVar2;
    short sVar3;
    int iVar4;
    BOOL bVar5;
    Actor* pAVar6;
    Sound* sound;
    short sVar9;
    short actor1y;
    short actor2y;
    uint32_t local_c;
    short maxSpriteWidth;

    ski_assert(actor1, 2350);
    ski_assert(actor2, 2351);

    if (actor1->typeMaybe >= 11) {
        return actor1;
    }
    actor1y = actor1->yPosMaybe;
    actor2y = actor2->yPosMaybe;
    pAVar6 = getLinkedActorIfExists(actor1);
    sVar2 = pAVar6->yPosMaybe;
    pAVar6 = getLinkedActorIfExists(actor2);
    sVar3 = pAVar6->yPosMaybe;
    if ((((actor1y < actor2y) || (sVar2 > sVar3)) && ((actor1y > actor2y || (sVar2 < sVar3)))) || ((actor1y == actor2y && (sVar2 == sVar3)))) {
        bVar5 = FALSE;
    } else {
        bVar5 = TRUE;
    }
    iVar4 = actor2->typeMaybe;
    local_c = actor1->frameNo;
    sVar1 = actor1->isInAir;
    sVar9 = actor2->spritePtr->height + actor2->isInAir;
    switch (actor1->typeMaybe) {
    case ACTOR_TYPE_10_WALKING_TREE:
        actor1->HorizontalVelMaybe = 0;
        return setActorFrameNo(actor1, 0x3c);
    case ACTOR_TYPE_5_YETI_TOP:
    case ACTOR_TYPE_6_YETI_BOTTOM:
    case ACTOR_TYPE_7_YETI_LEFT:
    case ACTOR_TYPE_8_YETI_RIGHT:
        if (actor2 == playerActor) {
            ski_assert(iVar4 == 0, 2393);
            playSound(&sound_7);
            if ((actor2->flags & FLAG_1) != 0) {
                actor2 = duplicateAndLinkActor(actor2);
            }
            actorSetFlag8IfFlag1IsUnset(actor2);
            ski_assert(actor1->permObject, 2396);

            actor1->permObject->actorFrameNo = 0x32;
            actor1->HorizontalVelMaybe = 0;
            actor1->permObject->xVelocity = 0;
            actor1->verticalVelocityMaybe = 0;
            actor1->permObject->yVelocity = 0;
            actor1->permObject->unk_0x20 = currentTickCount;
            return setActorFrameNo(actor1, 0x32);
        }
        break;
    case ACTOR_TYPE_0_PLAYER:
        if (local_c == 0x11)
            break;
        switch (iVar4) {
        case ACTOR_TYPE_15_BUMP:
            if (sVar1 < 1) {
                actor1->inAirCounter = 4;
                //                        LAB_00403cb4:
                addStylePoints(1);
                playSound(&sound_2);
                return setActorFrameNo(actor1, 0xd);
            }
            if (sVar9 <= sVar1)
                break;
            //                LAB_00403bcc:
            actor1->inAirCounter = actor1->verticalVelocityMaybe / 2;
            addStylePoints(1);
            sound = &sound_2;
            //                LAB_00403be8:
            playSound(sound);
            return setActorFrameNo(actor1, local_c);
        case ACTOR_TYPE_2_DOG:
        case ACTOR_TYPE_12_SLALOM_FLAG:
        case ACTOR_TYPE_17_SIGN:
            if (bVar5) {
                actor1->verticalVelocityMaybe = actor1->verticalVelocityMaybe / 2;
            }
            if (actor2->spriteIdx2 == 0x52) {
                addStylePoints(-16);
                return setActorFrameNo(actor1, local_c);
            }
            break;

        case ACTOR_TYPE_11_MOGULS:
            if (local_c == 0) {
                local_c = 0xd;
                actor1->inAirCounter = 1;
                if (actor1->verticalVelocityMaybe > 4) {
                    actor1->verticalVelocityMaybe = actor1->verticalVelocityMaybe / 2;
                    return setActorFrameNo(actor1, 0xd);
                }
            }
            break;

            // here

        case ACTOR_TYPE_14_ROCK_STUMP:
            if (0 < sVar1) {
                if (sVar9 < sVar1) {
                    if (actor2->spriteIdx2 == 0x56) {
                        if ((actor2->flags & FLAG_1) != 0) {
                            actor2 = duplicateAndLinkActor(actor2);
                        }
                        actorSetFlag8IfFlag1IsUnset(actor2);
                        addStylePoints(100);
                        return setActorFrameNo(actor1, local_c);
                    }
                    break;
                }
                if (!bVar5)
                    break;
                // goto LAB_00403bcc;
                actor1->inAirCounter = actor1->verticalVelocityMaybe / 2;
                addStylePoints(1);
                sound = &sound_2;
                //                LAB_00403be8:
                playSound(sound);
                return setActorFrameNo(actor1, local_c);
            }
        case ACTOR_TYPE_1_BEGINNER:
        case ACTOR_TYPE_3_SNOWBOARDER:
        case 4:
        case 9:
        case 10:
        case ACTOR_TYPE_13_TREE:
            if ((sVar9 < sVar1) || ((short)(actor1->spritePtr->height + sVar1) < actor2->isInAir)) {
                if (iVar4 == 9) {
                    addStylePoints(1000);
                    actor2->typeMaybe = ACTOR_TYPE_13_TREE;
                    actorSetSpriteIdx(actor2, 0x32);
                    return setActorFrameNo(actor1, local_c);
                } else {
                    addStylePoints(6);
                    return setActorFrameNo(actor1, local_c);
                }
            }
            if (bVar5) {
                if (iVar4 == ACTOR_TYPE_13_TREE) {
                    maxSpriteWidth = max_(actor1->spritePtr->width, actor2->spritePtr->width);
                    if (abs((int)actor1->xPosMaybe - (int)actor2->xPosMaybe) > (int)maxSpriteWidth / 2) {
                        actor1->verticalVelocityMaybe = actor1->verticalVelocityMaybe / 2;
                        return setActorFrameNo(actor1, local_c);
                    }
                }
                if ((sVar1 == 0) && (actor1->inAirCounter == 0)) {
                    local_c = 0xb;
                } else {
                    local_c = 0x11;
                    if (actor2->spriteIdx2 == 0x32) {
                        actor2->typeMaybe = 9;
                        setActorFrameNo(actor2, 0x38);
                        addStylePoints(0x10);
                        return setActorFrameNo(actor1, 0x11);
                    }
                }
                if ((actor1->verticalVelocityMaybe < 0) && (actor2->spriteIdx2 == 0x2e)) {
                    actorSetSpriteIdx(actor2, 0x56);
                    return setActorFrameNo(actor1, local_c);
                }
                addStylePoints(-0x20);
                playSound(&sound_1);
                return setActorFrameNo(actor1, local_c);
            }
            break;

        case ACTOR_TYPE_16_JUMP:
            if (((bVar5) && ((int)sVar1 < (int)sVar9 / 2)) && (0 < actor1->verticalVelocityMaybe)) {
                actor1->inAirCounter = actor1->verticalVelocityMaybe;
                //                        goto LAB_00403cb4;
                addStylePoints(1);
                playSound(&sound_2);
                return setActorFrameNo(actor1, 0xd);
            }
        }
        break;
    case ACTOR_TYPE_1_BEGINNER:
        if (0x18 < (int)local_c)
            break;
        if (iVar4 == 0) {
            addStylePoints(0x14);
        }
        sound = &sound_6;
        local_c = (0 < actor2->isInAir) + 0x19;
        //            goto LAB_00403be8;
        playSound(sound);
        return setActorFrameNo(actor1, local_c);
    case ACTOR_TYPE_2_DOG:
        if (((int)local_c < 0x1d) && ((actor2->HorizontalVelMaybe != 0 || (actor2->verticalVelocityMaybe != 0)))) {
            if (iVar4 == 0) {
                addStylePoints(3);
            }
            local_c = 0x1d;
            sound = &sound_3;
            //                goto LAB_00403be8;
            playSound(sound);
            return setActorFrameNo(actor1, local_c);
        }
        break;
    case ACTOR_TYPE_3_SNOWBOARDER:
        switch (iVar4) {
        case ACTOR_TYPE_0_PLAYER:
            addStylePoints(0x14);
        case ACTOR_TYPE_1_BEGINNER:
        case ACTOR_TYPE_3_SNOWBOARDER:
        case ACTOR_TYPE_13_TREE:
        case ACTOR_TYPE_14_ROCK_STUMP:
            if ((sVar1 < sVar9) && (local_c != 0x22)) {
                return setActorFrameNo(actor1, 0x22);
            }
            break;
        case ACTOR_TYPE_15_BUMP:
        case ACTOR_TYPE_16_JUMP:
            if (sVar1 < sVar9) {
                actor1->inAirCounter = actor1->verticalVelocityMaybe / 2;
                playSound(&sound_5);
                return setActorFrameNo(actor1, 0x21);
            }
        }
        break;
    case ACTOR_TYPE_4_CHAIRLIFT:
    case ACTOR_TYPE_9_TREE_ON_FIRE:
        break;
    default:
        assertFailed(sourceFilename, 2376);
    }
    return setActorFrameNo(actor1, local_c);
}

void updateAllPermObjectsInList(PermObjectList* param_1) {
    short top = (short)((windowClientRectWith120Margin.top - skierScreenYOffset) - 0x3c);
    short bottom = (short)((windowClientRectWith120Margin.bottom - skierScreenYOffset) + 0x3c);
    short sVar4;
    PermObject* permObject;

    permObject = param_1->startingObject;
    ski_assert(param_1, 2829);
    ski_assert(permObject <= param_1->nextObject, 2830);

    if (permObject < param_1->nextObject) {
        do {
            updatePermObject(permObject);
            sVar4 = permObject->maybeY - playerY;
            if ((sVar4 >= top) && (sVar4 < bottom)) {
                addActorForPermObject(permObject);
            }
            permObject++;
        } while (permObject < param_1->nextObject);
    }
}

Actor* addActorForPermObject(PermObject* permObject) {
    uint16_t newX;
    uint16_t newY;
    short inAir;
    Actor* actor;
    RECT spriteRect;

    ski_assert(permObject, 2604);
    if (!permObject->actor) {
        newX = permObject->maybeX;
        newY = permObject->maybeY;
        inAir = permObject->unk_0x18;
        updateRectForSpriteAtLocation(&spriteRect, permObject->spritePtr, newX, newY, inAir);
        if (doRectsOverlap(&spriteRect, &windowClientRectWith120Margin)) {
            if (permObject->spriteIdx == 0) {
                actor = addActorOfType(permObject->actorTypeMaybe, permObject->actorFrameNo);
            } else {
                actor = addActorOfTypeWithSpriteIdx(permObject->actorTypeMaybe, permObject->spriteIdx);
            }
            if (actor) {
                actor = updateActorPositionMaybe(actor, newX, newY, inAir);
                permObject->actor = actor;
                actor->permObject = permObject;
            }
        }
    }
    return permObject->actor;
}

static void movePermObject(PermObject* permObject) {
    int permObjectIndex;

    permObjectIndex = (int)(permObject - permObjects);
    permObject->maybeX += takeScaledMovement(permObject->xVelocity, &permObjectMovementRemainderX[permObjectIndex]);
    permObject->maybeY += takeScaledMovement(permObject->yVelocity, &permObjectMovementRemainderY[permObjectIndex]);
    permObject->unk_0x18 += takeScaledMovement(permObject->unk_0x1e, &permObjectMovementRemainderAir[permObjectIndex]);
}

// TODO not byte accurate
void updatePermObject(PermObject* permObject) {
    Actor* pAVar1;
    int actorType;

    if (permObject == (PermObject*)0x0) {
        assertFailed(sourceFilename, 2791);
    }
    movePermObject(permObject);
    actorType = permObject->actorTypeMaybe;
    if (actorType != ACTOR_TYPE_4_CHAIRLIFT) {
        // TODO this is byte accurate but probably not how it was originally written in C
        if ((actorType <= 4) || (8 < actorType)) {
            assertFailed(sourceFilename, 2809);
        } else {
            updateYeti(permObject);
        }
    } else {
        updatePermObjectActorType4(permObject);
    }

    pAVar1 = permObject->actor;
    if (pAVar1) {
        ski_assert(pAVar1, 2814); // TODO problems with deadcode removal here.
        ski_assert(pAVar1->permObject, 2815);
        ski_assert(pAVar1->permObject == permObject, 2816);

        pAVar1 = updateActorPositionMaybe(pAVar1, permObject->maybeX, permObject->maybeY, permObject->unk_0x18);
        setActorFrameNo(pAVar1, permObject->actorFrameNo);
    }
}

void updatePermObjectActorType4(PermObject* permObject) {
    ski_assert(permObject, 2633);
    ski_assert(permObject->actorTypeMaybe == ACTOR_TYPE_4_CHAIRLIFT, 2634);

    if (permObject->maybeY <= -1024) {
        permObject->actorFrameNo = 0x29;
        permObject->yVelocity = 2;
        permObject->maybeX = -144;
        return;
    }
    if (23552 <= permObject->maybeY) {
        permObject->actorFrameNo = 0x27;
        permObject->yVelocity = -2;
        permObject->maybeX = -112;
        return;
    }
    /* snowboarder jump out of chairlift */
    if (permObject->actor && (permObject->actorFrameNo == 0x27)) {
        if (ski_random(1000) == 0) {
            updateActorPositionMaybe(addActorOfType(ACTOR_TYPE_3_SNOWBOARDER, 0x21), permObject->maybeX, permObject->maybeY, permObject->unk_0x18);
            permObject->actorFrameNo = 0x28;
        }
    }
}

// TODO not byte accurate
void updateYeti(PermObject* permObject) {
    short permObjX;
    short permObjY;
    int iVar6;
    short sVar9;
    int actorFrameNo;
    short local_c;
    short pX;
    short pY;
    int tickRelated;
    int dX;
    int dY;

    sVar9 = 0;
    actorFrameNo = permObject->actorFrameNo;
    iVar6 = permObject->actorTypeMaybe;
    ski_assert(permObject, 2664);
    if (permObject->unk_0x18 > 0) {
        permObject->unk_0x1e--;
    } else {
        permObject->unk_0x1e = 0;
        permObject->unk_0x18 = 0;
    }
    if (permObject->unk_0x18 == 0) { // goto LAB_004046b8;

        if (actorFrameNo >= 0x32 && actorFrameNo < 0x38) {
            tickRelated = currentTickCount - permObject->unk_0x20;
            switch (actorFrameNo) {
            case 0x32:
                permObject->actorFrameNo = 0x33;
                return;
            case 0x33:
                permObject->actorFrameNo = (tickRelated < 500) ? 0x32 : 0x34; // ((499 < iVar6) - 1 & 0xfffffffe) + 0x34;
                return;
            case 0x34:
                if (tickRelated > 700) {
                    permObject->actorFrameNo = 0x35;
                    return;
                }
                break;
            case 0x35:
                if (tickRelated > 1000) {
                    permObject->actorFrameNo = 0x36;
                    return;
                }
                break;
            case 0x36:
                permObject->actorFrameNo = 0x37;
                return;
            case 0x37:
                permObject->actorFrameNo = tickRelated < 3000 ? 0x36 : 0x2a; // ((2999 < iVar6) - 1 & 0xc) + 0x2a;
                return;
            default:
                assertFailed(sourceFilename, 2678);
                permObject->actorFrameNo = actorFrameNo;
                return;
            }
            //        goto LAB_004046b8;
            permObject->actorFrameNo = actorFrameNo;
            return;
        }
        permObjX = permObject->maybeX;
        permObjY = permObject->maybeY;
        local_c = 0;
        if (iVar6 == ACTOR_TYPE_5_YETI_TOP && permObjY > -2000) {
            sVar9 = -10;
        } else if (iVar6 == ACTOR_TYPE_6_YETI_BOTTOM && permObjY < 32000) {
            sVar9 = 0x1a;
        } else if (iVar6 == ACTOR_TYPE_7_YETI_LEFT && permObjX > -16000) {
            local_c = -0x10;
        } else if (iVar6 == ACTOR_TYPE_8_YETI_RIGHT && permObjX < 16000) {
            local_c = 0x10;
        } else {
            //            LAB_004044dd:
            if (playerActor) {
                pX = playerActor->xPosMaybe;
                pY = playerActor->yPosMaybe;
                //                    sVar3 = playerActor->xPosMaybe;
                //                    sVar4 = playerActor->yPosMaybe;
                //                if (iVar6 == 5 && sVar4 < -2000) {
                //                    if () goto LAB_00404539;
                //                }
                //                else if (iVar6 == 6 && sVar4 > 32000) {
                //                    if () goto LAB_00404539;
                //                }
                //                else if (iVar6 == 7 && sVar3 < -16000)

                if ((iVar6 == ACTOR_TYPE_5_YETI_TOP && pY < -2000) || (iVar6 == ACTOR_TYPE_6_YETI_BOTTOM && pY > 32000) || (iVar6 == ACTOR_TYPE_7_YETI_LEFT && pX < -16000) || (iVar6 == ACTOR_TYPE_8_YETI_RIGHT && pX > 16000)) {
                    dX = (int)pX - (int)permObjX;
                    dY = (int)pY - (int)permObjY;
                    //                    sVar9 = (short) windowWidth;
                    //                        iVar10 = (int) sVar9;
                    if (dX > windowWidth) {
                        //                        sVar9 = -sVar9;
                        //                        LAB_00404564:
                        permObject->maybeX = pX + -windowWidth;
                    } else if (dX < -windowWidth) { //(dX >= -sVar9) {
                        // goto LAB_00404564; // (SBORROW4(dX,-iVar10) != dX + iVar10 < 0) goto LAB_00404564;
                        permObject->maybeX = pX + windowWidth;
                    }
                    //                        iVar10 = (int) windowHeight;
                    if (dY > windowHeight) {
                        //                        sVar9 = playerActor->yPosMaybe - windowHeight;
                        //                        LAB_00404598:
                        permObject->maybeY = playerActor->yPosMaybe - windowHeight;
                    } else if (dY < -windowHeight) { //(SBORROW4(dY,-iVar10) != dY + iVar10 < 0) {
                        //                        sVar9 = playerActor->yPosMaybe + windowHeight;
                        permObject->maybeY = playerActor->yPosMaybe + windowHeight;
                        //                        goto LAB_00404598;
                    }
                    if (dX >= 0x10) {
                        dX = 0x10;
                    } else {
                        if (dX <= -0x10) {
                            dX = -0x10;
                        }
                    }
                    local_c = (short)dX;
                    if (dY >= 0x1a) {
                        dY = 0x1a;
                    } else {
                        if (dY <= -10) {
                            dY = -10;
                        }
                    }
                    sVar9 = (short)dY;
                    playSound(&sound_9);
                }
            }
        }
        //    uVar11 = (uint32_t)local_c;
        //    uVar8 = (uint32_t)sVar9;

        if (abs(local_c) > abs(sVar9)) {
            permObject->yVelocity = (short)((int)((int)permObject->xVelocity * sVar9) / (int)local_c);
            //        LAB_00404617:
            permObject->unk_0x1e = 1;
        } else if (sVar9 != 0) {
            permObject->xVelocity = (short)((int)((int)permObject->yVelocity * local_c) / (int)sVar9);
            //        goto LAB_00404617;
            permObject->unk_0x1e = 1;
        }
        permObject->yVelocity = sVar9;
        permObject->xVelocity = local_c;
        if (sVar9 < 0) {
            permObject->actorFrameNo = (actorFrameNo == 0x30) + 0x30;
            return;
        }
        if (local_c < 0) {
            permObject->actorFrameNo = (actorFrameNo == 0x2c) + 0x2c;
            return;
        }
        if ((local_c <= 0) && (sVar9 <= 0)) {
            //            uVar5 = ski_random(10);
            if (ski_random(10) != 0) {
                permObject->actorFrameNo = 0x2a;
                return;
            }
            permObject->unk_0x1e = 4;
            permObject->actorFrameNo = 0x2b;
            return;
        }
        actorFrameNo = (actorFrameNo == 0x2e) + 0x2e;
    }
    //    LAB_004046b8:
    permObject->actorFrameNo = actorFrameNo;
}

void FUN_004046e0(PermObjectList* permObjList) {
    short top;
    short bottom;
    PermObject* permObject;
    PermObject* pPVar4;

    top = ((short)windowClientRectWith120Margin.top - skierScreenYOffset) + -0x3c;
    bottom = ((short)windowClientRectWith120Margin.bottom - skierScreenYOffset) + 0x3c;
    permObject = permObjList->currentObj;
    ski_assert(permObjList, 2849);
    ski_assert(permObject >= permObjList->startingObject, 2850);
    ski_assert(permObject <= permObjList->nextObject, 2851);

    pPVar4 = permObjList->nextObject;
    for (; permObject < pPVar4; permObject++) {
        if ((int)permObject->maybeY - (int)playerY >= (int)top)
            break;
    }

    for (; permObject > permObjList->startingObject; permObject--) {
        if ((int)permObject->maybeY - (int)playerY < (int)top)
            break;
    }

    permObjList->currentObj = permObject;

    for (; permObject < permObjList->nextObject;) {
        if ((int)permObject->maybeY - (int)playerY >= bottom) {
            break;
        }
        addActorForPermObject(permObject++);
    }
}

// TODO not byte accurate.
BOOL resetGame(void) {
    currentTickCount = SDL_GetTicks();
    // todo
    //  srand(currentTickCount);
    srand(1);
    setupActorList();
    playerActorPtrMaybe_1 = NULL;
    playerActor = NULL;
    totalAreaOfActorSprites = 0;
    resetPermObjectCount();
    isTurboMode = 0;
    playerY = 0;
    playerX = 0;
    DAT_0040c5d8 = 0;
    DAT_0040c714 = 0;
    stylePoints = 0;
    INT_0040c964 = 0;
    isSsGameMode = 0;
    INT_0040c960 = 0;
    isGsGameMode = 0;
    elapsedTime = 0;
    updateTimerDurationMillis = 40;
    redrawRequired = 1;
    return 1;
}

void deleteWindowObjects(void) {
    // if (smallBitmapSheet)
    // {
    //     DeleteObject(SelectObject(smallBitmapDC, smallBitmapSheet));
    // }
    // if (largeBitmapSheet)
    // {
    //     DeleteObject(SelectObject(largeBitmapDC, largeBitmapSheet));
    // }
    // if (smallBitmapSheet_1bpp)
    // {
    //     DeleteObject(SelectObject(smallBitmapDC_1bpp, smallBitmapSheet_1bpp));
    // }
    // if (largeBitmapSheet_1bpp)
    // {
    //     DeleteObject(SelectObject(largeBitmapDC_1bpp, largeBitmapSheet_1bpp));
    // }
    // if (scratchBitmap)
    // {
    //     DeleteObject(SelectObject(bitmapSourceDC, scratchBitmap));
    // }
    // if (smallBitmapDC)
    // {
    //     DeleteDC(smallBitmapDC);
    // }
    // if (largeBitmapDC)
    // {
    //     DeleteDC(largeBitmapDC);
    // }
    // if (smallBitmapDC_1bpp)
    // {
    //     DeleteDC(smallBitmapDC_1bpp);
    // }
    // if (largeBitmapDC_1bpp)
    // {
    //     DeleteDC(largeBitmapDC_1bpp);
    // }
    // if (bitmapSourceDC)
    // {
    //     DeleteDC(bitmapSourceDC);
    // }
}

int getSkierGroundSpriteFromMousePosition(short param_1, short param_2) {
    short uVar1;

    if (param_2 > 0) {
        if (param_1 == 0) {
            return 0;
        }
        uVar1 = (((int)param_2 << 2) / (int)param_1);
        if (uVar1 <= -0xc) {
            return 0;
        }
        if (uVar1 <= -6) {
            return 1;
        }
        if (uVar1 <= -3) {
            return 2;
        }
        if (uVar1 <= -1) {
            return 3;
        }
        if (uVar1 >= 0xc) {
            return 0;
        }
        if (uVar1 >= 6) {
            return 4;
        }
        if (uVar1 >= 3) {
            return 5;
        }
        if (uVar1 >= 1) {
            return 6;
        }
    }
    return param_1 < 0 ? 3 : 6;
}

int getSkierInAirSpriteFromMousePosition(short param_1, short param_2) {
    if (param_1 < 0) {
        if (param_2 < 0) {
            return (param_2 < param_1) ? 16 : 14;
        }
        return (-param_2 < param_1) ? 13 : 14;
    }
    if (param_2 < 0) {
        return (-param_2 <= param_1) ? 15 : 16;
    }
    return (param_2 <= param_1) + 13;
}

void handleMouseClick() {
    uint32_t ActorframeNo;

    if (!playerActor) {
        handleGameReset();
        return;
    }
    ActorframeNo = playerActor->frameNo;
    if (ActorframeNo != 11) {
        if (playerActor->isInAir == 0) {
            playerActor->inAirCounter = 4;
            ActorframeNo = 0xd;
        } else if (ActorframeNo != 17) {
            switch (ActorframeNo) {
            case 0xd:
                ActorframeNo = 0x12;
                break;
            case 0x12:
                ActorframeNo = 0x13;
                break;
            case 0x13:
                ActorframeNo = 0xd;
                break;
            case 0xe:
                ActorframeNo = 0x14;
                break;
            case 0xf:
                ActorframeNo = 0x15;
                break;
            }
        }
    }
    if ((ActorframeNo != playerActor->frameNo) && (setActorFrameNo(playerActor, ActorframeNo), redrawRequired != 0)) {
        drawWindow(mainWindowDC, &windowClientRect);
        redrawRequired = FALSE;
    }
}

void handleMouseMoveMessage(short xPos, short yPos) {
    int ActorframeNo;
    short mouseSkierXDelta;
    short mouseSkierYDelta;

    if (((DAT_0040c760 != 0) && (((xPos != prevMouseX || (yPos != prevMouseY)) && (playerActor != NULL)))) && ((playerActor->frameNo != 0xb && (playerActor->frameNo != 0x11)))) {
        mouseSkierXDelta = xPos - (short)skierScreenXOffset;
        mouseSkierYDelta = yPos - (short)skierScreenYOffset;
        if (playerActor->isInAir == 0) {
            ActorframeNo = getSkierGroundSpriteFromMousePosition(mouseSkierXDelta, mouseSkierYDelta);
        } else {
            ActorframeNo = getSkierInAirSpriteFromMousePosition(mouseSkierXDelta, mouseSkierYDelta);
        }
        setActorFrameNo(playerActor, ActorframeNo);
    }
    prevMouseX = xPos;
    prevMouseY = yPos;
    DAT_0040c760 = 1;
}

// TODO not byte accurate
void updateWindowSize(HWND hWnd) {
    DAT_0040c760 = 0;
    int w, h;

    SDL_GetRendererOutputSize(renderer, &w, &h);
    windowClientRect.right = w;
    windowClientRect.bottom = h;

    updateActorsAfterWindowResize(
        (windowClientRect.left + windowClientRect.right) / 2,
        (windowClientRect.top + windowClientRect.bottom) / 3);
    windowClientRectWith120Margin.left = windowClientRect.left - 120;
    windowClientRectWith120Margin.right = windowClientRect.right + 120;
    windowClientRectWith120Margin.bottom = windowClientRect.bottom + 120;
    windowClientRectWith120Margin.top = windowClientRect.top - 120;
    windowWidth = (short)(windowClientRect.right - windowClientRect.left);
    windowHeight = (short)(windowClientRect.bottom - windowClientRect.top);
    windowWithMarginTotalArea = (windowClientRectWith120Margin.bottom - windowClientRectWith120Margin.top) * (windowClientRectWith120Margin.bottom - windowClientRectWith120Margin.left);
}

// TODO not byte accurate
void updateActorsAfterWindowResize(short centreX, short centreY) {
    Actor* actor;

    for (actor = actorListPtr; actor != NULL; actor = actor->next) {
        if (((actor->flags & FLAG_4) != 0) && ((actor->flags & FLAG_2) == 0)) {
            if ((actor->flags & FLAG_1) != 0) {
                duplicateAndLinkActor(actor);
            }
            actor->flags &= 0xfffffffb;
        }
    }

    skierScreenYOffset = centreY;
    skierScreenXOffset = centreX;
}

// TODO not byte accurate
void updateRectForSpriteAtLocation(RECT* rect, Sprite* sprite, short newX, short newY, short param_5) {
    short spriteHeight;
    short spriteWidth;

    spriteWidth = sprite->width;
    spriteHeight = sprite->height;
    ski_assert(rect, 907);
    ski_assert(sprite, 908);

    rect->top = ((short)(newY + (short)((short)(skierScreenYOffset - playerY) - param_5)) - spriteHeight);
    rect->left = (newX + (short)((skierScreenXOffset - spriteWidth / 2) - playerX));
    rect->bottom = spriteHeight + rect->top;
    rect->right = spriteWidth + rect->left;
}

void formatAndPrintStatusStrings(HDC windowDC) {
    short sVar1;
    short speed;
    short x = statusWindowLabelWidth + 2;
    short y = 2;
    char strBuf[20];
    int len;

    speed = 0;
    sVar1 = 0;

    if (playerActor != NULL) {
        if (updateTimerDurationMillis != 0) {
            speed = (short)((int)(playerActor->verticalVelocityMaybe * 1000) / (int)(updateTimerDurationMillis * 16));
        } else {
            speed = 0;
        }
        sVar1 = playerActor->yPosMaybe;
        if (isSsGameMode) {
            sVar1 = 8640 - sVar1;
        } else {
            if (isFsGameMode) {
                sVar1 = 16640 - sVar1;
            } else {
                if (isGsGameMode) {
                    sVar1 = 16640 - sVar1;
                }
            }
        }
    }

    SDL_LockTextureToSurface(statusWindowTexture, NULL, &statusWindowSurface);
    SDL_Rect rect;
    rect.x = x;
    rect.y = 1;
    rect.w = statusWindowSurface->w - x - 1;
    rect.h = statusWindowSurface->h - 2;
    SDL_FillRect(statusWindowSurface, &rect, SDL_MapRGB(statusWindowSurface->format, 255, 255, 255));

    len = formatElapsedTime(elapsedTime, strBuf);
    drawText(windowDC, strBuf, x, &y, len);
    sprintf(strBuf, getCachedString(IDS_DIST_FORMAT), (short)(sVar1 / 16));
    len = strlen(strBuf);
    drawText(windowDC, strBuf, x, &y, len);

    sprintf(strBuf, getCachedString(IDS_SPEED_FORMAT), speed);
    len = strlen(strBuf);
    drawText(windowDC, strBuf, x, &y, len);

    sprintf(strBuf, getCachedString(IDS_STYLE_FORMAT), stylePoints);
    drawText(windowDC, strBuf, x, &y, len);
    statusWindowLastUpdateTime = currentTickCount;

    SDL_UnlockTexture(statusWindowTexture);
}

PermObject* addPermObject(PermObjectList* objList, PermObject* permObject) {
    PermObject* pPVar1;

    pPVar1 = &permObjects[permObjectCount++]; // + uVar2;
    ski_assert(objList, 2587);
    ski_assert(permObject, 2588);
    ski_assert(permObjectCount <= NUM_PERM_OBJECTS, 2589);

    if (objList->startingObject == (PermObject*)0x0) {
        objList->currentObj = pPVar1;
        objList->nextObject = pPVar1;
        objList->startingObject = pPVar1;
    }
    ski_assert(objList->nextObject == pPVar1, 2592);
    objList->nextObject = objList->nextObject + 1;
    memcpy(pPVar1, permObject, sizeof(PermObject));
    permObjectMovementRemainderX[pPVar1 - permObjects] = 0.0f;
    permObjectMovementRemainderY[pPVar1 - permObjects] = 0.0f;
    permObjectMovementRemainderAir[pPVar1 - permObjects] = 0.0f;
    pPVar1->actor = NULL;
    pPVar1->spritePtr = &sprites[(uint16_t)pPVar1->spriteIdx];

    return pPVar1;
}

void setupPermObjects() {
    BOOL bVar1;
    uint16_t uVar2;
    PermObject* pPVar3;
    short sVar4;
    PermObject permObject;

    permObject.unk_0x1e = 0;
    permObject.yVelocity = 0;
    permObject.xVelocity = 0;
    permObject.unk_0x18 = 0;
    setPointerToNull(&PermObjectList_0040c630);
    permObject.actorTypeMaybe = ACTOR_TYPE_17_SIGN;

    // Slalom sign
    permObject.maybeX = ((short)playerX - skierScreenXOffset) + windowClientRect.left + 60;
    permObject.spriteIdx = 0x3d;
    if (permObject.maybeX < -320) {
        permObject.maybeX = -320;
    }
    permObject.maybeY = ((short)playerY - skierScreenYOffset) + windowClientRect.bottom - 60;
    if (640 < permObject.maybeY) {
        permObject.maybeY = 520;
    }
    addPermObject(&PermObjectList_0040c630, &permObject);

    // Slalom start sign left
    permObject.spriteIdx = 0x39;
    permObject.maybeX = -576;
    permObject.maybeY = 640;
    addPermObject(&PermObjectList_0040c630, &permObject);

    // Slalom start sign right
    permObject.spriteIdx = 0x3a;
    permObject.maybeX = -320;
    addPermObject(&PermObjectList_0040c630, &permObject);
    permObject.actorTypeMaybe = ACTOR_TYPE_12_SLALOM_FLAG;
    bVar1 = TRUE;
    firstSlalomFlagLeft = (PermObject*)0x0;
    /* slalom flags */
    sVar4 = 960;
    do {
        permObject.spriteIdx = bVar1 ? 0x17 : 0x18; // 0x18 - (uint16_t)bVar1;
        permObject.maybeX = bVar1 ? -496 : -400;    //(-(uint16_t)bVar1 & 0xffa0) + -400;
        bVar1 = !bVar1;
        permObject.maybeY = sVar4;
        pPVar3 = addPermObject(&PermObjectList_0040c630, &permObject);
        if (firstSlalomFlagLeft == (PermObject*)0x0) {
            firstSlalomFlagLeft = pPVar3;
        }
        sVar4 = sVar4 + 320;
    } while (sVar4 < 8640);
    permObject.actorTypeMaybe = ACTOR_TYPE_17_SIGN;
    permObject.spriteIdx = 0x3b;
    permObject.maybeX = -576;
    permObject.maybeY = 8640;
    addPermObject(&PermObjectList_0040c630, &permObject);
    permObject.spriteIdx = 0x3c;
    permObject.maybeX = -320;
    addPermObject(&PermObjectList_0040c630, &permObject);
    setPointerToNull(&PermObjectList_0040c5e0);
    permObject.actorTypeMaybe = ACTOR_TYPE_17_SIGN;
    permObject.spriteIdx = 0x3e;
    permObject.maybeX = ((short)windowClientRect.right - skierScreenXOffset) + -0x3c + (short)playerX;
    if (permObject.maybeX > 320) {
        permObject.maybeX = 320;
    }
    permObject.maybeY = ((short)windowClientRect.bottom - skierScreenYOffset) + playerY + -0x3c;
    if (0x280 < permObject.maybeY) {
        permObject.maybeY = 520;
    }
    addPermObject(&PermObjectList_0040c5e0, &permObject);
    permObject.spriteIdx = 0x39;
    permObject.maybeX = 320;
    permObject.maybeY = 640;
    addPermObject(&PermObjectList_0040c5e0, &permObject);
    permObject.spriteIdx = 0x3a;
    permObject.maybeX = 512;
    addPermObject(&PermObjectList_0040c5e0, &permObject);
    bVar1 = TRUE;
    FirstSlalomFlagRight = (PermObject*)0x0;
    /* slalom flags, right hand side */
    sVar4 = 0x410;
    do {
        permObject.actorTypeMaybe = ACTOR_TYPE_12_SLALOM_FLAG;
        permObject.spriteIdx = bVar1 ? 0x17 : 0x18;
        permObject.maybeX = bVar1 ? 400 : 432;
        bVar1 = !bVar1;
        permObject.maybeY = sVar4;
        pPVar3 = addPermObject(&PermObjectList_0040c5e0, &permObject);
        if (FirstSlalomFlagRight == (PermObject*)0x0) {
            FirstSlalomFlagRight = pPVar3;
        }
        permObject.actorTypeMaybe = ACTOR_TYPE_13_TREE;
        permObject.spriteIdx = getSpriteIdxForActorType(ACTOR_TYPE_13_TREE);
        uVar2 = ski_random(0x20);
        permObject.maybeX = uVar2 + 400;
        /* this should use the return value */
        ski_random(400);
        sVar4 = sVar4 + 400;
    } while (sVar4 < 0x4100);
    permObject.actorTypeMaybe = ACTOR_TYPE_17_SIGN;
    /* finish sign left */
    permObject.spriteIdx = 0x3b;
    permObject.maybeX = 320;
    permObject.maybeY = 16640;
    addPermObject(&PermObjectList_0040c5e0, &permObject);
    /* finish sign right */
    permObject.spriteIdx = 0x3c;
    permObject.maybeX = 512;
    addPermObject(&PermObjectList_0040c5e0, &permObject);
    setPointerToNull(&PermObjectList_0040c658);

    permObject.actorTypeMaybe = ACTOR_TYPE_17_SIGN;
    permObject.spriteIdx = 0x3f;
    permObject.maybeX = 0;
    permObject.maybeY = (windowClientRect.bottom - skierScreenYOffset);
    permObject.maybeY += playerY;
    permObject.maybeY -= 0x3c;
    if (0x280 < permObject.maybeY) {
        permObject.maybeY = 520;
    }
    addPermObject(&PermObjectList_0040c658, &permObject);
    permObject.spriteIdx = 0x39;
    permObject.maybeX = -160;
    permObject.maybeY = 640;
    addPermObject(&PermObjectList_0040c658, &permObject);
    permObject.spriteIdx = 0x3a;
    permObject.maybeX = 160;
    addPermObject(&PermObjectList_0040c658, &permObject);
    permObject.spriteIdx = 0x3b;
    permObject.maybeX = -160;
    permObject.maybeY = 16640;
    addPermObject(&PermObjectList_0040c658, &permObject);
    permObject.spriteIdx = 0x3c;
    permObject.maybeX = 160;
    addPermObject(&PermObjectList_0040c658, &permObject);
    INT_0040c968 = 0;
    isFsGameMode = 0;
    setPointerToNull(&PermObjectList_0040c738);
    /* ski lift poles??? */
    sVar4 = -1024;
    do {
        permObject.actorTypeMaybe = ACTOR_TYPE_13_TREE;
        permObject.spriteIdx = 0x40;
        permObject.maybeX = -128;
        permObject.maybeY = sVar4;
        permObject.unk_0x18 = 0;
        permObject.unk_0x1e = 0;
        permObject.yVelocity = 0;
        permObject.xVelocity = 0;
        addPermObject(&PermObjectList_0040c738, &permObject);
        sVar4 = sVar4 + 2048;
    } while (sVar4 <= 23552);
    setPointerToNull(&PermObjectList_0040c720);
    sVar4 = -1024;
    do {
        permObject.actorTypeMaybe = ACTOR_TYPE_4_CHAIRLIFT;
        permObject.spriteIdx = 0;
        permObject.unk_0x1e = 0;
        permObject.xVelocity = 0;
        permObject.unk_0x18 = 0x20;
        permObject.maybeY = sVar4;
        if (-1024 < sVar4) {
            permObject.actorFrameNo = 0x27;
            permObject.maybeX = -112;
            permObject.yVelocity = -2;
            addPermObject(&PermObjectList_0040c720, &permObject);
        }
        if (sVar4 < 23552) {
            permObject.actorFrameNo = 0x29;
            permObject.maybeX = -144;
            permObject.yVelocity = 2;
            addPermObject(&PermObjectList_0040c720, &permObject);
        }
        sVar4 = sVar4 + 2048;
    } while (sVar4 <= 23552);
    permObject.actorTypeMaybe = ACTOR_TYPE_7_YETI_LEFT;
    permObject.actorFrameNo = 0x2a;
    permObject.spriteIdx = 0;
    permObject.maybeX = -16060;
    permObject.unk_0x18 = 0;
    permObject.maybeY = 0;
    permObject.unk_0x1e = 0;
    permObject.yVelocity = 0;
    permObject.xVelocity = 0;
    addPermObject(&PermObjectList_0040c720, &permObject);
    permObject.actorTypeMaybe = ACTOR_TYPE_8_YETI_RIGHT;
    permObject.maybeX = 16060;
    addPermObject(&PermObjectList_0040c720, &permObject);
    permObject.actorTypeMaybe = ACTOR_TYPE_5_YETI_TOP;
    permObject.maybeX = 0;
    permObject.maybeY = -2060;
    addPermObject(&PermObjectList_0040c720, &permObject);
    permObject.actorTypeMaybe = ACTOR_TYPE_6_YETI_BOTTOM;
    permObject.maybeY = 32060;
    addPermObject(&PermObjectList_0040c720, &permObject);
}

// TODO not byte accurate
void handleKeydownMessage(SDL_Event* e) {
    short sVar1;
    uint32_t actorframeNo;

    switch (e->key.keysym.sym) {
    case SDLK_ESCAPE:
        // ShowWindow(hSkiMainWnd, 6 /*SW_MINIMIZE*/);
        SDL_MinimizeWindow(hSkiMainWnd);
        return;
    case SDLK_F3:
        togglePausedState(); // TODO this is a jmp rather than a call in the original
        return;
    case SDLK_RETURN:
        if (playerActor != (Actor*)0x0) {
            return;
        }
        handleGameReset(); // TODO this is a jmp rather than a call in the original
        return;
    case SDLK_F2:
        handleGameReset(); // TODO this is a jmp rather than a call in the original
        return;
    default:
        break;
    }

    if (playerActor == NULL) {
        return;
    }
    actorframeNo = playerActor->frameNo;
    sVar1 = playerActor->isInAir;
    if ((actorframeNo != 0xb) && (actorframeNo != 0x11)) {
        switch (e->key.keysym.sym) {
        case SDLK_LEFT:
            /* numpad 4
               left */
            ski_assert(actorframeNo < 0x16, 0xf63);

            actorframeNo = playerTurnFrameNoTbl[actorframeNo].leftFrameNo;
            if (actorframeNo == 7) {
                //                    iVar2 = (int)playerActor->HorizontalVelMaybe - 8;
                //                    if (iVar2 <= -8) {
                //                        iVar2 = -8;
                //                    }
                //                    playerActor->HorizontalVelMaybe = (short) iVar2;
                playerActor->HorizontalVelMaybe = max_(playerActor->HorizontalVelMaybe - 8, -8);
            }
            break;

        case SDLK_RIGHT:
            /* numpad 6, Right */
            ski_assert(actorframeNo < 0x16, 3947);

            actorframeNo = playerTurnFrameNoTbl[actorframeNo].rightFrameNo;
            if (actorframeNo == 8) {
                //                    iVar2 = (int) playerActor->HorizontalVelMaybe + 8;
                //                    if (iVar2 >= 8) {
                //                        iVar2 = 8;
                //                    }
                //                    playerActor->HorizontalVelMaybe = (short) iVar2;

                playerActor->HorizontalVelMaybe = min_(playerActor->HorizontalVelMaybe + 8, 8);
            }
            break;

        case SDLK_DOWN:
            if (sVar1 == 0) {
                actorframeNo = 0;
                break;
            }
            switch (actorframeNo) {
            case 0xd:
                actorframeNo = 0x13;
                break;
            case 0x14:
                actorframeNo = 0xe;
                break;
            case 0x15:
                actorframeNo = 0xf;
                break;
            case 0x12:
                actorframeNo = 0xd;
                break;
            case 0x13:
                actorframeNo = 0x12;
                break;
            }
            break;

        case SDLK_UP:
            switch (actorframeNo) {
            case 0xd:
                //                switchD_0040628c_caseD_13:
                actorframeNo = 0x12;
                break;
            case 0x13:
                //                switchD_0040628c_caseD_12:
                actorframeNo = 0xd;
                break;
            case 0xe:
                actorframeNo = 0x14;
                break;
            case 0xf:
                actorframeNo = 0x15;
                break;
            case 3:
            case 7:
            case 0xc:
                if (playerActor->verticalVelocityMaybe == 0) {
                    actorframeNo = 9;
                    playerActor->verticalVelocityMaybe = -4;
                }
                break;
            case 6:
            case 8:
                if (playerActor->verticalVelocityMaybe == 0) {
                    actorframeNo = 10;
                    playerActor->verticalVelocityMaybe = -4;
                }
                break;
            case 0x12:
                //                switchD_0040628c_caseD_d:
                actorframeNo = 0x13;
                break;
            }
            break;

        case SDLK_KP_7:
            if (sVar1 == 0) {
                actorframeNo = 3;
            }
            break;

        case 0x21:
        case 0x69:
        case SDLK_KP_9:
            /* numpad 9
               Up right */
            if (sVar1 == 0) {
                actorframeNo = 6;
            }
            break;

        case 0x23:
        case 0x61:
            /* numpad 1
               down left */
            if (sVar1 == 0) {
                actorframeNo = 1;
            }
            break;

        case 0x22:
        case 99:
            /* numpad 3
               down right */
            if (sVar1 == 0) {
                actorframeNo = 4;
            }
            break;

        case 0x2d:
        case 0x60:
            /* numpad 0, Insert
               Jump. */
            if (sVar1 == 0) {
                playerActor->inAirCounter = 2;
                actorframeNo = 0xd;
                if (4 < playerActor->verticalVelocityMaybe) {
                    playerActor->verticalVelocityMaybe = playerActor->verticalVelocityMaybe + -4;
                }
            }
        }
    }

    if ((actorframeNo != playerActor->frameNo) && (setActorFrameNo(playerActor, actorframeNo), redrawRequired != 0)) {
        drawWindow(mainWindowDC, &windowClientRect);
        redrawRequired = 0;
    }
}

void updateGameState() {
    int iVar1;
    Actor* actor;
    RECT* ptVar6;
    RECT* rect2;
    Actor* pAVar7;

    DAT_0040c714 = DAT_0040c714 - (short)playerX;
    DAT_0040c5d8 = DAT_0040c5d8 - playerY;

    for (pAVar7 = actorListPtr; pAVar7 != NULL; pAVar7 = pAVar7->next) {
        if ((pAVar7->flags & (FLAG_2 | FLAG_8)) == 0) {
            pAVar7->flags &= 0xffffffdf;
            if ((pAVar7->permObject == NULL) && (pAVar7->typeMaybe < 0xb)) {
                updateActor(pAVar7);
            }
            if (((pAVar7->flags & FLAG_1) == 0) && (pAVar7 != playerActor)) {
                if ((pAVar7->flags & FLAG_4) != 0) {
                    ptVar6 = &pAVar7->someRect;
                } else {
                    ptVar6 = updateActorSpriteRect(pAVar7);
                }
                if (doRectsOverlap(ptVar6, &windowClientRectWith120Margin) == FALSE) {
                    totalAreaOfActorSprites = totalAreaOfActorSprites - pAVar7->spritePtr->totalPixels;
                    actorSetFlag8IfFlag1IsUnset(pAVar7);
                }
            }
        }
    }

    FUN_004046e0(&PermObjectList_0040c630);
    FUN_004046e0(&PermObjectList_0040c5e0);
    FUN_004046e0(&PermObjectList_0040c658);
    FUN_004046e0(&PermObjectList_0040c738);
    updateAllPermObjectsInList(&PermObjectList_0040c720);
    removeFlag8ActorsFromList();
    for (pAVar7 = actorListPtr; pAVar7 != NULL; pAVar7 = pAVar7->next) {
        if ((pAVar7->flags & FLAG_2) == 0) {
            if ((pAVar7->flags & FLAG_4) != 0) {
                ptVar6 = &pAVar7->someRect;
            } else {
                ptVar6 = updateActorSpriteRect(pAVar7);
            }
            // testing FLAG_20
            iVar1 = pAVar7->flags << 26;
            iVar1 = iVar1 >> 31; //(pAVar7->flags & FLAG_20) ? TRUE : FALSE;
            for (actor = actorListPtr; (actor != NULL && (pAVar7 != actor)); actor = actor->next) {
                if (((actor->flags & FLAG_2) == 0) && (iVar1 != 0 || ((actor->flags & FLAG_20) != 0))) {
                    if ((actor->flags & FLAG_4) != 0) {
                        rect2 = &actor->someRect;
                    } else {
                        rect2 = updateActorSpriteRect(actor);
                    }

                    if (doRectsOverlap(ptVar6, rect2)) {
                        handleActorCollision(pAVar7, actor);
                        if ((pAVar7->flags & FLAG_8) == 0 && (actor->flags & FLAG_8) == 0) {
                            handleActorCollision(actor, pAVar7);
                        }
                    }
                }
            }
        }
    }
    DAT_0040c714 = DAT_0040c714 + (short)playerX;
    for (DAT_0040c5d8 = DAT_0040c5d8 + playerY; 0x3c < DAT_0040c5d8;
         DAT_0040c5d8 = DAT_0040c5d8 + -0x3c) {
        addRandomActor(BORDER_BOTTOM);
    }
    for (; DAT_0040c5d8 < -0x3c; DAT_0040c5d8 = DAT_0040c5d8 + 0x3c) {
        addRandomActor(BORDER_TOP);
    }
    for (; 0x3c < DAT_0040c714; DAT_0040c714 = DAT_0040c714 + -0x3c) {
        addRandomActor(BORDER_RIGHT);
    }
    for (; DAT_0040c714 < -0x3c; DAT_0040c714 = DAT_0040c714 + 0x3c) {
        addRandomActor(BORDER_LEFT);
    }

    if (ski_random(0x29a) != 0) {
        return;
    }
    pAVar7 = addActorOfType(ACTOR_TYPE_3_SNOWBOARDER, 0x1f);

    /* top of screen */
    updateActorWithOffscreenStartingPosition(pAVar7, BORDER_TOP);
}

static BOOL actorUsesVelocityMovement(const Actor* actor) {
    switch (actor->typeMaybe) {
    case ACTOR_TYPE_0_PLAYER:
        return actor->frameNo != 0xb;
    case ACTOR_TYPE_1_BEGINNER:
        return actor->frameNo < 25;
    case ACTOR_TYPE_2_DOG:
    case ACTOR_TYPE_3_SNOWBOARDER:
    case ACTOR_TYPE_10_WALKING_TREE:
        return TRUE;
    default:
        return FALSE;
    }
}

static void updateMovementOnly(void) {
    Actor* actor;
    Actor* nextActor;
    PermObject* permObject;

    for (actor = actorListPtr; actor != NULL; actor = nextActor) {
        nextActor = actor->next;
        if ((actor->flags & (FLAG_2 | FLAG_8)) == 0 && actor->permObject == NULL && actorUsesVelocityMovement(actor)) {
            updateActorPositionWithVelocityMaybe(actor);
        }
    }

    for (permObject = PermObjectList_0040c720.startingObject;
         permObject != NULL && permObject < PermObjectList_0040c720.nextObject;
         permObject++) {
        movePermObject(permObject);
        if (permObject->actor != NULL) {
            updateActorPositionMaybe(permObject->actor, permObject->maybeX, permObject->maybeY, permObject->unk_0x18);
        }
    }
}

// TODO not byte accurate register usage is swapped.
BOOL createBitmapSheets(HDC param_1) {
    HBITMAP bitmap;
    int resourceId;
    Sprite* sprite;
    short maxWidth;
    int smallBitmapSheetHeight;
    int largeBitmapSheetHeight;
    int largeSpriteYOffset;
    int smallSpriteYOffset;
    int sheetYOffset;
    short maxHeight;
    int spriteWidth;
    int spriteHeigth;

    SDL_Surface* currentSurface;

    sprites->sheetDC = NULL;
    sprites->sheetDC_1bpp = NULL;
    sprites->sheetYOffset = 0;
    sprites->width = 0;
    sprites->height = 0;
    sprites->totalPixels = 0;
    maxWidth = 0;
    maxHeight = 0;
    largeBitmapSheetHeight = 0;
    smallBitmapSheetHeight = 0;
    smallSpriteYOffset = 0;
    largeSpriteYOffset = 0;

    for (resourceId = 1; resourceId < 90; resourceId++) {
        bitmap = loadBitmapResource(resourceId);
        if (bitmap == NULL) {
            return FALSE;
        }
        if (bitmap->w > maxWidth) {
            maxWidth = bitmap->w;
        }
        if (bitmap->h > maxHeight) {
            maxHeight = bitmap->h;
        }
        if (bitmap->w > 32) {
            largeBitmapSheetHeight += bitmap->h;
        } else {
            smallBitmapSheetHeight += bitmap->h;
        }
        SDL_FreeSurface(bitmap);
    }

    // smallBitmapDC = CreateCompatibleDC(param_1);
    // if (!smallBitmapDC)
    // {
    //     return FALSE;
    // }
    // pHVar1 = CreateCompatibleBitmap(param_1, 32, (int)(short)smallBitmapSheetHeight);
    // if (pHVar1 == (HBITMAP)0x0)
    // {
    //     return FALSE;
    // }
    // smallBitmapSheet = SelectObject(smallBitmapDC, pHVar1);
    // if (smallBitmapSheet == (HGDIOBJ)0x0)
    // {
    //     DeleteObject(pHVar1);
    //     return FALSE;
    // }
    smallBitmapDC = SDL_CreateRGBSurface(0, 32, smallBitmapSheetHeight, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000);

    // smallBitmapDC_1bpp = CreateCompatibleDC(param_1);
    // if (smallBitmapDC_1bpp == (HDC)0x0)
    // {
    //     return FALSE;
    // }
    // pHVar1 = CreateBitmap(32, (int)(short)smallBitmapSheetHeight, 1, 1, (void *)0x0);
    // if (pHVar1 == (HBITMAP)0x0)
    // {
    //     return FALSE;
    // }
    // smallBitmapSheet_1bpp = SelectObject(smallBitmapDC_1bpp, pHVar1);
    // if (smallBitmapSheet_1bpp == (HGDIOBJ)0x0)
    // {
    //     DeleteObject(pHVar1);
    //     return FALSE;
    // }
    // largeBitmapDC = CreateCompatibleDC(param_1);
    // if (largeBitmapDC == (HDC)0x0)
    // {
    //     return FALSE;
    // }
    // pHVar1 = CreateCompatibleBitmap(param_1, (int)(short)(uint16_t)maxWidth, (int)(short)largeBitmapSheetHeight);
    // if (pHVar1 == (HBITMAP)0x0)
    // {
    //     return FALSE;
    // }
    // largeBitmapSheet = SelectObject(largeBitmapDC, pHVar1);
    // if (largeBitmapSheet == (HGDIOBJ)0x0)
    // {
    //     DeleteObject(pHVar1);
    //     return FALSE;
    // }
    largeBitmapDC = SDL_CreateRGBSurface(0, maxWidth, largeBitmapSheetHeight, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
    // largeBitmapDC_1bpp = CreateCompatibleDC(param_1);
    // if (largeBitmapDC_1bpp == (HDC)0x0)
    // {
    //     return FALSE;
    // }
    // pHVar1 = CreateBitmap((int)(short)(uint16_t)maxWidth, (int)(short)largeBitmapSheetHeight, 1, 1,
    //                       (void *)0x0);
    // if (pHVar1 == (HBITMAP)0x0)
    // {
    //     return FALSE;
    // }

    // largeBitmapSheet_1bpp = SelectObject(largeBitmapDC_1bpp, pHVar1);
    // if (largeBitmapSheet_1bpp == (HGDIOBJ)0x0)
    // {
    //     DeleteObject(pHVar1);
    //     return FALSE;
    // }
    // bitmapSourceDC = CreateCompatibleDC(param_1);
    for (resourceId = 1; resourceId < NUM_SPRITES; resourceId++) {
        sprite = &sprites[resourceId];
        bitmap = loadBitmapResource(resourceId);
        if (bitmap == (HBITMAP)0x0) {
            return FALSE;
        }
        // GetObjectA(pHVar1, sizeof(BITMAP), &bitmap);

        spriteWidth = bitmap->w;
        spriteHeigth = bitmap->h;

        sprite->width = spriteWidth;
        sprite->height = spriteHeigth;
        sprite->totalPixels = (spriteWidth * spriteHeigth);
        if (spriteWidth > SMALL_TEXTURE_ATLAS_MAX_SIZE) {
            sheetYOffset = largeSpriteYOffset;
            largeSpriteYOffset += spriteHeigth;
            currentSurface = largeBitmapDC;
        } else {
            sheetYOffset = smallSpriteYOffset;
            smallSpriteYOffset += spriteHeigth;
            currentSurface = smallBitmapDC;
        }
        sprite->sheetYOffset = sheetYOffset;
        sprite->sheetDC = spriteWidth > SMALL_TEXTURE_ATLAS_MAX_SIZE ? largeBitmapDC : smallBitmapDC;
        sprite->sheetDC_1bpp = spriteWidth > SMALL_TEXTURE_ATLAS_MAX_SIZE ? largeBitmapDC_1bpp : smallBitmapDC_1bpp;

        SDL_Rect dstrect;
        dstrect.x = 0;
        dstrect.y = sheetYOffset;
        dstrect.w = bitmap->w;
        dstrect.h = bitmap->h;
        SDL_BlitSurface(bitmap, NULL, currentSurface, &dstrect);

        // BitBlt(sprite->sheetDC, 0, sheetYOffset, bitmap.bmWidth, bitmap.bmHeight, bitmapSourceDC, 0, 0,
        //         0xcc0020);
        //  BitBlt(sprite->sheetDC_1bpp, 0, sheetYOffset, bitmap.bmWidth, bitmap.bmHeight,
        //         bitmapSourceDC, 0, 0, 0x330008);
        //  pvVar2 = SelectObject(bitmapSourceDC, pvVar2);
        //  DeleteObject(pvVar2);
    }

    SDL_SetColorKey(largeBitmapDC, SDL_TRUE, SDL_MapRGB(largeBitmapDC->format, 0xFF, 0xFF, 0xFF));
    SDL_SetColorKey(smallBitmapDC, SDL_TRUE, SDL_MapRGB(smallBitmapDC->format, 0xFF, 0xFF, 0xFF));

    largeTextureAtlas = SDL_CreateTextureFromSurface(renderer, largeBitmapDC);
    smallTextureAtlas = SDL_CreateTextureFromSurface(renderer, smallBitmapDC);

    for (resourceId = 1; (uint16_t)resourceId < 90; resourceId++) {
        sprite = &sprites[resourceId & 0xffff];
        if (sprite->width > 32) {
            sprite->sheet = largeTextureAtlas;
        } else {
            sprite->sheet = smallTextureAtlas;
        }
    }

    // scratchBitmapWidth = ((uint16_t)maxWidth & 0xffc0) + 0x40;
    // scratchBitmapHeight = ((uint16_t)maxHeight & 0xffc0) + 0x40;

    // pHVar1 = CreateCompatibleBitmap(param_1, (int)scratchBitmapWidth, (int)scratchBitmapHeight);
    // if (pHVar1 == (HBITMAP)0x0)
    // {
    //     return FALSE;
    // }
    // scratchBitmap = SelectObject(bitmapSourceDC, pHVar1);
    // if (scratchBitmap == (HGDIOBJ)0x0)
    // {
    //     DeleteObject(pHVar1);
    //     return FALSE;
    // }
    return TRUE;
}

// TODO not byte accurate
void drawWindow(HDC hdc, RECT* windowRect) {
    RECT* rect1;
    RECT* ptVar3;
    uint32_t uVar4;
    Actor* pAVar6;
    Actor* pAVar7;

    // ski_assert(hdc, 1272);
    ski_assert(windowRect, 1273);

    for (pAVar7 = actorListPtr; pAVar7 != NULL; pAVar7 = pAVar7->next) {
        /* if FLAG_1 FLAG_2 FLAG_8 are unset */
        if ((pAVar7->flags & (FLAG_1 | FLAG_2 | FLAG_8)) == 0) {
            pAVar6 = pAVar7->linkedActor;
            if (pAVar6 != NULL && (pAVar6->flags & FLAG_1) != 0 && (pAVar6->flags & FLAG_2) != 0 && pAVar7->spriteIdx2 == pAVar6->spriteIdx2) {
                if ((pAVar6->flags & FLAG_4) != 0) {
                    ptVar3 = &pAVar6->someRect;
                } else {
                    ptVar3 = updateActorSpriteRect(pAVar6);
                }
                if ((pAVar7->flags & FLAG_4) != 0) {
                    rect1 = &pAVar7->someRect;
                } else {
                    rect1 = updateActorSpriteRect(pAVar7);
                }
                if (areRectanglesEqual(rect1, ptVar3)) {
                    pAVar7->flags |= FLAG_1;
                    pAVar6->flags = pAVar6->flags &= 0xfffffffe;
                    actorSetFlag8IfFlag1IsUnset(pAVar6);
                }
            }
        }
    }
    for (pAVar7 = actorListPtr; pAVar7 != NULL; pAVar7 = pAVar7->next) {
        if ((pAVar7->flags & FLAG_8) != 0) {
            pAVar7->flags &= 0xffffffef;
        } else {
            if ((pAVar7->flags & FLAG_4) != 0) {
                ptVar3 = &pAVar7->someRect;
            } else {
                ptVar3 = updateActorSpriteRect(pAVar7);
            }
            uVar4 = doRectsOverlap(ptVar3, windowRect);
            /* set FLAG_10 if rects overlap */
            pAVar7->flags = (pAVar7->flags & 0xffffffef) | ((uVar4 & 1) << 4);
            if ((uVar4 & 1) != 0) {
                (pAVar7->rect).left = ptVar3->left;
                (pAVar7->rect).top = ptVar3->top;
                (pAVar7->rect).right = ptVar3->right;
                (pAVar7->rect).bottom = ptVar3->bottom;
                pAVar7->actorPtr = NULL;
            }
        }
    }

    for (pAVar7 = actorListPtr; pAVar7 != NULL; pAVar7 = pAVar7->next) {
        if ((pAVar7->flags & FLAG_10) != 0) {
            pAVar6 = pAVar7->linkedActor;

            if (pAVar6 != NULL && (pAVar6->flags & FLAG_10) != 0 && doRectsOverlap(&pAVar7->rect, &pAVar6->rect)) {
                actorClearFlag10(pAVar7, pAVar6);
            }

            for (pAVar6 = actorListPtr; pAVar6 != NULL && pAVar6 != pAVar7; pAVar6 = pAVar6->next) {
                if ((pAVar6->flags & FLAG_10) != 0 && doRectsOverlap(&pAVar7->rect, &pAVar6->rect)) {
                    actorClearFlag10(pAVar7, pAVar6);
                    pAVar6 = actorListPtr;
                }
            }
        }
    }

    for (pAVar7 = actorListPtr; pAVar7 != NULL; pAVar7 = pAVar7->next) {
        if ((pAVar7->flags & FLAG_10) != 0) {
            drawActor(hdc, pAVar7);
        }
    }

    for (pAVar7 = actorListPtr; pAVar7 != NULL; pAVar7 = pAVar7->next) {
        if ((pAVar7->flags & FLAG_2) != 0) {
            actorSetFlag8IfFlag1IsUnset(pAVar7);
        }
    }
    removeFlag8ActorsFromList();
}

void drawActor(HDC hdc, Actor* actor) {
    Actor** ppAVar1;
    short y;
    RECT* rect;
    uint16_t uVar6;
    Actor* pAVar7;
    short sVar8;
    short newWidth;
    uint16_t newHeight;
    Actor* actor_00;
    short sVar10;
    Actor** ppAVar11;
    // DWORD rop;
    Actor* local_24;
    Actor* local_20;
    int local_1c;
    uint32_t local_14;
    uint32_t actorRectLeft;
    short sheetY;
    int local_c;
    uint32_t local_8;
    Actor** local_4;
    Sprite* sprite;
    short spriteWidth;
    short spriteHeight;

    uVar6 = *(uint16_t*)&(actor->rect).left;
    actorRectLeft = (actor->rect).left;

    y = *(short*)&(actor->rect).top;
    newWidth = *(short*)&(actor->rect).right - uVar6;
    newHeight = *(short*)&(actor->rect).bottom - y;

    local_c = 0;
    local_20 = actor;
    ski_assert(actor, 1133);
    // ski_assert(hdc, 1134);
    ski_assert((actor->flags & FLAG_10) != 0, 1135);

    if (actor == NULL) {
        return;
    }

    pAVar7 = actor;
    while ((pAVar7->flags & FLAG_1) << 1 != (pAVar7->flags & FLAG_2)) {
        pAVar7 = pAVar7->actorPtr;
        if (pAVar7 == NULL) {
            return;
        }
    }
    local_1c = 0;
    if (!changeScratchBitmapSize(newWidth, newHeight)) {
        // PatBlt(hdc, (int)(short)uVar6, (int)y, (int)newWidth, (int)(short)newHeight, 0xff0062);
        do {
            if ((actor->flags & FLAG_1) == 0 && (actor->flags & FLAG_2) == 0) {
                sprite = actor->spritePtr;
                if ((actor->flags & FLAG_4) == 0) {
                    rect = updateActorSpriteRect(actor);
                } else {
                    rect = &actor->someRect;
                }

                // SRCAND
                // BitBlt(hdc, rect->left, rect->top, (int)sprite->width, (int)sprite->height, sprite->sheetDC, 0, (int)sprite->sheetYOffset, 0x8800c6);
                actor->flags |= FLAG_1;
            } else {
                actor->flags &= 0xfffffffe;
            }
            actor = actor->actorPtr;
        } while (actor != NULL);
        return;
    }

    do {
        actor_00 = NULL;
        local_24 = NULL;
        pAVar7 = actor;
        ppAVar11 = &local_20;
        if (actor == NULL)
            break;
        do {
            ppAVar1 = &actor->actorPtr;
            if ((actor->flags & FLAG_2) == 0) {
                if ((actor->flags & FLAG_40) == 0) {
                    sVar8 = 0;
                } else {
                    sVar8 = actor->spritePtr->height;
                }
                uVar6 = actor->yPosMaybe - sVar8;
                if ((actor_00 == NULL) || ((short)uVar6 < (short)local_8)) {
                    actor_00 = actor;
                    local_24 = actor;
                    local_8 = uVar6; // actor->flags & 0xffff0000 | (uint32_t)uVar6;
                    local_4 = ppAVar11;
                }
            } else {
                if ((actor->flags & FLAG_1) != 0) {
                    local_c = 1;
                    actor->flags &= 0xfffffffe;
                }
                *ppAVar11 = *ppAVar1;
                pAVar7 = local_20;
            }
            actor = *ppAVar1;
            ppAVar11 = ppAVar1;
        } while (actor != NULL);
        actor = pAVar7;
        if (actor_00 != NULL) {
            sprite = actor_00->spritePtr;
            spriteWidth = sprite->width;
            spriteHeight = sprite->height;
            sheetY = sprite->sheetYOffset;
            if ((actor_00->flags & FLAG_4) == 0) {
                rect = updateActorSpriteRect(actor_00);
            } else {
                rect = &actor_00->someRect;
            }

            local_14 = rect->left - actorRectLeft;
            sVar10 = *(short*)&rect->top - y;
            ski_assert(rect->right - rect->left == spriteWidth, 1203);
            ski_assert(rect->bottom - rect->top == spriteHeight, 1204);
            ski_assert(local_14 >= 0, 1205);

            // if (sVar10 < 0) {
            //     printf("skipping because sVar10\n");
            //     return;
            // }

            ski_assert(sVar10 >= 0, 1206);
            ski_assert(newWidth >= spriteWidth, 1207);

            // if (sVar10 < 0) {
            //     printf("skipping because sVar10\n");
            //     return;
            // }
            if (newHeight < spriteHeight) {
                assertFailed(sourceFilename, 1208);
            }
            if (local_1c == 0) {
                local_1c = 1;
                if ((((0 < (short)local_14) || (0 < sVar10)) || (spriteWidth < newWidth)) || (spriteHeight < newHeight)) {
                    // PatBlt(bitmapSourceDC, 0, 0, (int)newWidth, (int)newHeight, 0xff0062);
                }
                // rop = 0xcc0020;
            } else {
                // SRCPAINT
                // BitBlt(bitmapSourceDC, (int)(short)local_14, (int)sVar10, spriteWidth, spriteHeight, sprite->sheetDC_1bpp, 0, (int)sheetY,
                //        0xee0086);
                // SRCAND
                // rop = 0x8800c6;
            }

            // BitBlt(bitmapSourceDC, (int)(short)local_14, (int)sVar10, spriteWidth, spriteHeight, sprite->sheetDC, 0, (int)sheetY, rop);

            SDL_Rect srcrect;
            srcrect.x = 0;
            srcrect.y = sheetY;
            srcrect.w = spriteWidth;
            srcrect.h = spriteHeight;

            SDL_Rect dstrect;
            dstrect.x = rect->left;
            dstrect.y = rect->top;
            dstrect.w = spriteWidth;
            dstrect.h = spriteHeight;
            SDL_RenderCopy(renderer, sprite->sheet, &srcrect, &dstrect);

            local_24->flags |= FLAG_1;
            *local_4 = local_24->actorPtr;
            actor = local_20;
        }
    } while (actor != NULL);
    if (local_1c != 0) {
        // SRCCOPY
        // BitBlt(hdc, (int)(short)actorRectLeft, (int)y, (int)newWidth, (int)(short)newHeight, bitmapSourceDC, 0, 0, 0xcc0020);

        return;
    }
    if (local_c != 0) {
        // PatBlt(hdc, (int)(short)actorRectLeft, (int)y, (int)newWidth, (int)(short)newHeight, 0xff0062);
    }
}
