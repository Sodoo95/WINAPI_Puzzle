#pragma region Include + namespaces + Gdiplus setup
#include <Windows.h>
#include <gdiplus.h>
#include <Shlwapi.h>
#include <algorithm>
#include <random>
#include <chrono>
#include "resource.h"

#pragma comment(lib, "Shlwapi.lib")
#pragma comment (lib, "gdiplus.lib")

using namespace Gdiplus;
using namespace std::chrono;
ULONG_PTR gdiplusToken;
#pragma endregion
#pragma region Globals
#define WindowWidth 1600
#define WindowHeight 900

bool isRunning;
bool isLeftClicked;
bool isLeftClickProcessed = false;
bool debugMode = false;

enum GameState { Menu, Playing, GameOver };
enum GameLevel { One, Two, Three, Four, Five };
#pragma endregion
#pragma region Functions
Bitmap* CropBitmap(Bitmap* source, int x, int y, int width, int height) {
	Bitmap* cropped = new Bitmap(width, height, PixelFormat32bppARGB);
	Graphics g(cropped);
	g.DrawImage(source, Rect(0, 0, width, height), x, y, width, height, UnitPixel);
	return cropped;
}

Bitmap* LoadPngFromResource(HINSTANCE hInstance, UINT resourceID)
{
	HRSRC hRes = FindResource(hInstance, MAKEINTRESOURCE(resourceID), L"PNG");
	if (!hRes) return nullptr;

	DWORD size = SizeofResource(hInstance, hRes);
	if (size == 0) return nullptr;

	HGLOBAL hMem = LoadResource(hInstance, hRes);
	if (!hMem) return nullptr;

	void* pData = LockResource(hMem);
	if (!pData) return nullptr;

	IStream* pStream = SHCreateMemStream((BYTE*)pData, size);
	if (!pStream) return nullptr;

	Bitmap* bmp = Bitmap::FromStream(pStream);
	pStream->Release();

	if (!bmp || bmp->GetLastStatus() != Ok) {
		delete bmp;
		return nullptr;
	}

	return bmp;
}

Bitmap* ResizeBitmap(Bitmap* src, UINT newWidth, UINT newHeight)
{
	Bitmap* dst = new Bitmap(newWidth, newHeight, PixelFormat32bppARGB);
	Graphics g(dst);
	g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
	g.DrawImage(src, 0, 0, newWidth, newHeight);
	return dst;
}
#pragma endregion
#pragma region Structs
struct piece {
	unsigned int id;
	Bitmap* bmp;
	Point pos;
	bool movable;
};
#pragma endregion
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_LBUTTONDOWN:
		isLeftClicked = true;
		isLeftClickProcessed = false;
		break;
	case WM_LBUTTONUP:
		isLeftClicked = false;
		isLeftClickProcessed = false;
		break;
	case WM_KEYDOWN:
		if (wParam == 'P') debugMode = true;
		if (wParam == 'O') debugMode = false;
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}
class Game {
private:
	int GRID_ROWS;
	int GRID_COLS;
	int PIECE_SIZE;
	int GRID_OFFSET_X;
	int GRID_OFFSET_Y;
	int MAX_PIECES;

	int grid[4][4];  // Max 4x4 grid

	Bitmap* bmp_Background;
	Bitmap* bmp_OneFull;
	Bitmap* bmp_TwoFull;
	Bitmap* bmp_ThreeFull;
	Bitmap* bmp_FourFull;
	Bitmap* bmp_FiveFull;
	Bitmap* bmp_m_one;
	Bitmap* bmp_m_two;
	Bitmap* bmp_m_three;
	Bitmap* bmp_m_four;
	Bitmap* bmp_m_five;
	Bitmap* bmp_back;

	piece pieces[12];  // Max pieces for 4x3 grid
	GameState currentState = Menu;
	GameLevel currentLevel = One;
	bool levelInitialized = false;

	// Timer and stats
	steady_clock::time_point startTime;
	int timeRemaining = 200;
	int clickCount = 0;
	bool levelCleared[5] = { false, false, false, false, false };
	int clearedTime[5] = { 0, 0, 0, 0, 0 };
	int clearedClicks[5] = { 0, 0, 0, 0, 0 };

#pragma region Helper Functions
	bool ScreenToGrid(int screenX, int screenY, int& gridRow, int& gridCol) {
		int adjX = screenX - GRID_OFFSET_X;
		int adjY = screenY - GRID_OFFSET_Y;

		if (adjX < 0 || adjY < 0) return false;

		gridCol = adjX / PIECE_SIZE;
		gridRow = adjY / PIECE_SIZE;

		if (gridRow >= GRID_ROWS || gridCol >= GRID_COLS) return false;

		return true;
	}

	Point GridToScreen(int gridRow, int gridCol) {
		return Point(
			GRID_OFFSET_X + gridCol * PIECE_SIZE,
			GRID_OFFSET_Y + gridRow * PIECE_SIZE
		);
	}

	piece* GetPieceAtGrid(int gridRow, int gridCol) {
		if (gridRow < 0 || gridRow >= GRID_ROWS ||
			gridCol < 0 || gridCol >= GRID_COLS) {
			return nullptr;
		}

		int pieceId = grid[gridRow][gridCol];
		if (pieceId == -1) return nullptr;

		for (int i = 0; i < MAX_PIECES; i++) {
			if (pieces[i].id == pieceId) {
				return &pieces[i];
			}
		}
		return nullptr;
	}

	bool IsSolved() {
		int expectedId = 0;
		for (int row = 0; row < GRID_ROWS; row++) {
			for (int col = 0; col < GRID_COLS; col++) {
				if (expectedId == MAX_PIECES - 1) {
					if (grid[row][col] != -1 && grid[row][col] != expectedId) {
						return false;
					}
				}
				else {
					if (grid[row][col] != expectedId) {
						return false;
					}
				}
				expectedId++;
			}
		}
		return true;
	}

	void AutoComplete() {
		// Fill grid in correct order
		int id = 0;
		for (int row = 0; row < GRID_ROWS; row++) {
			for (int col = 0; col < GRID_COLS; col++) {
				if (id == MAX_PIECES - 1) {
					grid[row][col] = -1; // Empty space
				}
				else {
					grid[row][col] = id;
					// Update piece position
					for (int i = 0; i < MAX_PIECES; i++) {
						if (pieces[i].id == id) {
							pieces[i].pos = GridToScreen(row, col);
							break;
						}
					}
				}
				id++;
			}
		}
	}

	void InitializeLevel(Bitmap* fullImage) {
		// Clear grid
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				grid[i][j] = -1;
			}
		}

		// Reset timer and clicks
		startTime = steady_clock::now();
		timeRemaining = 200;
		clickCount = 0;

		// Crop all pieces
		int temp = 0;
		for (int row = 0; row < GRID_ROWS; row++) {
			for (int col = 0; col < GRID_COLS; col++) {
				Bitmap* bm = CropBitmap(fullImage,
					col * PIECE_SIZE, row * PIECE_SIZE,
					PIECE_SIZE, PIECE_SIZE);
				pieces[temp].bmp = bm;
				pieces[temp].id = temp;
				pieces[temp].movable = true;
				temp++;
			}
		}

		// Shuffle pieces
		std::random_device rd;
		std::mt19937 gen(rd());
		std::shuffle(std::begin(pieces), std::begin(pieces) + MAX_PIECES, gen);

		// Build initial grid
		temp = 0;
		for (int row = 0; row < GRID_ROWS; row++) {
			for (int col = 0; col < GRID_COLS; col++) {
				grid[row][col] = pieces[temp].id;
				pieces[temp].pos = GridToScreen(row, col);
				temp++;
			}
		}

		// Mark last piece as empty space
		for (int row = 0; row < GRID_ROWS; row++) {
			for (int col = 0; col < GRID_COLS; col++) {
				if (grid[row][col] == MAX_PIECES - 1) {
					grid[row][col] = -1;
				}
			}
		}

		levelInitialized = true;
	}

	void UpdateTimer() {
		if (IsSolved()) return; // Don't update if solved

		auto now = steady_clock::now();
		auto elapsed = duration_cast<seconds>(now - startTime).count();
		timeRemaining = 200 - (int)elapsed;

		if (timeRemaining <= 0) {
			timeRemaining = 0;
			currentState = GameOver;
		}
	}
#pragma endregion

public:
	void Start(HINSTANCE hInstance) {
		bmp_OneFull = LoadPngFromResource(hInstance, IDB_ONE);
		bmp_TwoFull = LoadPngFromResource(hInstance, IDB_TWO);
		bmp_ThreeFull = LoadPngFromResource(hInstance, IDB_THREE);
		bmp_FourFull = LoadPngFromResource(hInstance, IDB_FOUR);
		bmp_FiveFull = LoadPngFromResource(hInstance, IDB_FIVE);
		bmp_Background = LoadPngFromResource(hInstance, IDB_PNG1);
		bmp_m_one = LoadPngFromResource(hInstance, IDB_PNG2);
		bmp_m_two = LoadPngFromResource(hInstance, IDB_PNG3);
		bmp_m_three = LoadPngFromResource(hInstance, IDB_PNG4);
		bmp_m_four = LoadPngFromResource(hInstance, IDB_PNG5);
		bmp_m_five = LoadPngFromResource(hInstance, IDB_PNG6);
		bmp_back = LoadPngFromResource(hInstance, IDB_BACK);
	}

	void HandleInput(HWND hWnd) {
		// Check for cheat code
		if (GetAsyncKeyState('K') & 0x8000) {
			if (currentState == Playing && !IsSolved()) {
				AutoComplete();
			}
		}
	}

	void Render(HWND hWnd) {
#pragma region Rendering Setup
		HDC hdc = GetDC(hWnd);
		HDC memDC = CreateCompatibleDC(hdc);
		HBITMAP memBmp = CreateCompatibleBitmap(hdc, WindowWidth, WindowHeight);
		HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
		Graphics graphics(memDC);
		graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
#pragma endregion

		POINT p;
		GetCursorPos(&p);
		ScreenToClient(hWnd, &p);

		switch (currentState) {
		case Menu: {
			graphics.DrawImage(bmp_back, 0, 0);

			// Title
			SolidBrush titleBrush(Color(255, 255, 255, 240));
			FontFamily fontFamily(L"Meiryo");
			Font titleFont(&fontFamily, 72, FontStyleBold, UnitPixel);
			graphics.DrawString(L"ファン・ゴッホの人生", -1, &titleFont, PointF(400, 100), &titleBrush);

			// Description
			SolidBrush descBrush(Color(255, 255, 255, 200));
			Font descFont(&fontFamily, 24, FontStyleRegular, UnitPixel);
			graphics.DrawString(L"ファン・ゴッホの絵画を通じて彼の人生の旅を体験してください", -1, &descFont, PointF(0, 800), &descBrush);

			// Draw level previews
			int levelX[] = { 100, 400, 700, 1000, 1300 };
			for (int i = 0; i < 5; i++) {
				Bitmap* preview = nullptr;
				Bitmap* fullImage = nullptr;

				if (levelCleared[i]) {
					// Show full colored image if cleared
					switch (i) {
					case 0: fullImage = bmp_OneFull; break;
					case 1: fullImage = bmp_TwoFull; break;
					case 2: fullImage = bmp_ThreeFull; break;
					case 3: fullImage = bmp_FourFull; break;
					case 4: fullImage = bmp_FiveFull; break;
					}
					preview = ResizeBitmap(fullImage, 200, 200);
				}
				else {
					// Show monochrome preview
					switch (i) {
					case 0: preview = ResizeBitmap(bmp_m_one, 200, 200); break;
					case 1: preview = ResizeBitmap(bmp_m_two, 200, 200); break;
					case 2: preview = ResizeBitmap(bmp_m_three, 200, 200); break;
					case 3: preview = ResizeBitmap(bmp_m_four, 200, 200); break;
					case 4: preview = ResizeBitmap(bmp_m_five, 200, 200); break;
					}
				}

				graphics.DrawImage(preview, levelX[i], 400);
				delete preview;

				// Draw "CLEARED" text if level is cleared
				if (levelCleared[i]) {
					SolidBrush clearBrush(Color(255, 100, 255, 100));
					Font clearFont(&fontFamily, 20, FontStyleBold, UnitPixel);
					graphics.DrawString(L"クリア済み", -1, &clearFont, PointF(levelX[i] + 40, 610), &clearBrush);
				}
			}

			// Click detection
			if (isLeftClicked && !isLeftClickProcessed) {
				for (int i = 0; i < 5; i++) {
					if (p.x >= levelX[i] && p.x <= levelX[i] + 200 && p.y >= 400 && p.y <= 600) {
						currentState = Playing;
						currentLevel = (GameLevel)i;
						levelInitialized = false;
						isLeftClickProcessed = true;
						break;
					}
				}
			}
		} break;

		case Playing: {
			if (!levelInitialized) {
				switch (currentLevel) {
				case One:
					GRID_COLS = 3;
					GRID_ROWS = 3;
					PIECE_SIZE = 200;
					GRID_OFFSET_X = 150;
					GRID_OFFSET_Y = 50;
					MAX_PIECES = 9;
					InitializeLevel(bmp_OneFull);
					break;
				case Two:
					GRID_COLS = 3;
					GRID_ROWS = 3;
					PIECE_SIZE = 200;
					GRID_OFFSET_X = 150;
					GRID_OFFSET_Y = 50;
					MAX_PIECES = 9;
					InitializeLevel(bmp_TwoFull);
					break;
				case Three:
					GRID_COLS = 3;
					GRID_ROWS = 3;
					PIECE_SIZE = 200;
					GRID_OFFSET_X = 150;
					GRID_OFFSET_Y = 50;
					MAX_PIECES = 9;
					InitializeLevel(bmp_ThreeFull);
					break;
				case Four:
					GRID_COLS = 4;
					GRID_ROWS = 3;
					PIECE_SIZE = 150;
					GRID_OFFSET_X = 150;
					GRID_OFFSET_Y = 100;
					MAX_PIECES = 12;
					InitializeLevel(bmp_FourFull);
					break;
				case Five:
					GRID_COLS = 4;
					GRID_ROWS = 3;
					PIECE_SIZE = 150;
					GRID_OFFSET_X = 150;
					GRID_OFFSET_Y = 100;
					MAX_PIECES = 12;
					InitializeLevel(bmp_FiveFull);
					break;
				}
			}

			UpdateTimer();

			graphics.DrawImage(bmp_Background, 0, 0);

			// Draw preview image
			Bitmap* previewImage = nullptr;
			switch (currentLevel) {
			case One: previewImage = ResizeBitmap(bmp_OneFull, 300, 300); break;
			case Two: previewImage = ResizeBitmap(bmp_TwoFull, 300, 300); break;
			case Three: previewImage = ResizeBitmap(bmp_ThreeFull, 300, 300); break;
			case Four: previewImage = ResizeBitmap(bmp_FourFull, 300, 300); break;
			case Five: previewImage = ResizeBitmap(bmp_FiveFull, 300, 300); break;
			}
			if (previewImage) {
				graphics.DrawImage(previewImage, 1075, 50);
				delete previewImage;
			}

			// Draw timer
			SolidBrush timerBrush(Color(255, 255, 100, 100));
			FontFamily fontFamily(L"Arial");
			Font timerFont(&fontFamily, 48, FontStyleBold, UnitPixel);
			wchar_t timerText[50];
			swprintf_s(timerText, L"時間: %d", timeRemaining);
			graphics.DrawString(timerText, -1, &timerFont, PointF(1050, 400), &timerBrush);

			// Draw click count
			SolidBrush clickBrush(Color(255, 100, 100, 255));
			wchar_t clickText[50];
			swprintf_s(clickText, L"クリック数: %d", clickCount);
			graphics.DrawString(clickText, -1, &timerFont, PointF(1050, 470), &clickBrush);

			// Draw all pieces
			for (int row = 0; row < GRID_ROWS; row++) {
				for (int col = 0; col < GRID_COLS; col++) {
					piece* pc = GetPieceAtGrid(row, col);
					if (pc != nullptr) {
						Point pos = GridToScreen(row, col);
						graphics.DrawImage(pc->bmp, pos.X, pos.Y, PIECE_SIZE, PIECE_SIZE);
					}
				}
			}

			// Check if solved
			if (IsSolved()) {
				if (!levelCleared[currentLevel]) {
					levelCleared[currentLevel] = true;
					clearedTime[currentLevel] = timeRemaining;
					clearedClicks[currentLevel] = clickCount;
				}

				SolidBrush greenBrush(Color(255, 50, 255, 50));
				FontFamily meiryo(L"Meiryo");
				Font bigFont(&meiryo, 64, FontStyleBold, UnitPixel);
				graphics.DrawString(L"クリア !!!", -1, &bigFont, PointF(1060, 570), &greenBrush);

				Font smallFont(&meiryo, 28, FontStyleBold, UnitPixel);
				SolidBrush whiteBrush(Color(255, 255, 255, 255));
				wchar_t resultText[200];
				swprintf_s(resultText, L"残り時間: %d秒\nクリック数: %d回", clearedTime[currentLevel], clearedClicks[currentLevel]);
				graphics.DrawString(resultText, -1, &smallFont, PointF(1050, 660), &whiteBrush);

				// Fill in the empty space
				for (int row = 0; row < GRID_ROWS; row++) {
					for (int col = 0; col < GRID_COLS; col++) {
						if (grid[row][col] == -1) {
							grid[row][col] = MAX_PIECES - 1;
						}
					}
				}
			}
			else {
				// Draw level description
				SolidBrush textBrush(Color(255, 255, 255, 255));
				FontFamily meiryo(L"Meiryo");
				Font font(&meiryo, 20, FontStyleRegular, UnitPixel);

				const wchar_t* levelText = L"";
				switch (currentLevel) {
				case One:
					levelText = L"「ジャガイモを食べる人々」(1885)\n暗い色調で農民の生活を描く\n初期のオランダ時代";
					break;
				case Two:
					levelText = L"「ひまわり」(1888)\n鮮やかな黄色で描かれた\n最も有名な作品の一つ";
					break;
				case Three:
					levelText = L"「星月夜」(1889)\n渦巻く夜空の幻想的な風景\nサン=レミ療養院にて";
					break;
				case Four:
					levelText = L"「夜のカフェテラス」(1888)\nアルルの夜の情景を描く\n星空の下の温かい光";
					break;
				case Five:
					levelText = L"「アイリス」(1889)\n精神病院の庭で描いた花\n生命力と希望の象徴";
					break;
				}
				graphics.DrawString(levelText, -1, &font, PointF(1050, 560), &textBrush);
			}

			// Handle piece movement
			int hoverRow, hoverCol;
			if (ScreenToGrid(p.x, p.y, hoverRow, hoverCol)) {
				Pen pen(Color(255, 0, 0));
				Point screenPos = GridToScreen(hoverRow, hoverCol);
				if (debugMode) {
					graphics.DrawRectangle(&pen, screenPos.X, screenPos.Y, PIECE_SIZE, PIECE_SIZE);
				}

				piece* currentPiece = GetPieceAtGrid(hoverRow, hoverCol);

				if (currentPiece != nullptr && !IsSolved()) {
					int directions[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };

					for (int i = 0; i < 4; i++) {
						int checkRow = hoverRow + directions[i][0];
						int checkCol = hoverCol + directions[i][1];

						if (checkRow >= 0 && checkRow < GRID_ROWS &&
							checkCol >= 0 && checkCol < GRID_COLS &&
							grid[checkRow][checkCol] == -1) {

							Point emptySpace = GridToScreen(checkRow, checkCol);

							if (debugMode) {
								pen.SetColor(Color(0, 0, 255));
								graphics.DrawRectangle(&pen, emptySpace.X, emptySpace.Y, PIECE_SIZE, PIECE_SIZE);
							}

							if (isLeftClicked && !isLeftClickProcessed) {
								grid[checkRow][checkCol] = currentPiece->id;
								grid[hoverRow][hoverCol] = -1;
								currentPiece->pos = emptySpace;
								isLeftClickProcessed = true;
								clickCount++;
								break;
							}
						}
					}
				}
			}

			// Back button
			SolidBrush buttonBrush(Color(100, 50, 50, 50));
			graphics.FillRectangle(&buttonBrush, 1400, 800, 150, 50);
			SolidBrush textBrush(Color(255, 255, 255, 255));
			FontFamily arial(L"Meiryo");
			Font font(&arial, 24, FontStyleBold, UnitPixel);
			graphics.DrawString(L"メニュー", -1, &font, PointF(1410, 810), &textBrush);

			if (isLeftClicked && !isLeftClickProcessed &&
				p.x >= 1400 && p.x <= 1550 && p.y >= 800 && p.y <= 850) {
				currentState = Menu;
				levelInitialized = false;
				isLeftClickProcessed = true;
			}

			SolidBrush helperBrush(Color(180, 200, 200, 200));
			FontFamily courierFont(L"Courier New");
			Font helperFont(&courierFont, 24, FontStyleBold, UnitPixel);
			graphics.DrawString(L"K - Cheat  |  P - Debug ON  |  O - Debug OFF", -1, &helperFont, PointF(150, 800), &helperBrush);


		} break;

		case GameOver: {
			graphics.DrawImage(bmp_back, 0, 0);

			// Game Over text
			SolidBrush redBrush(Color(255, 255, 50, 50));
			FontFamily fontFamily(L"Meiryo");
			Font bigFont(&fontFamily, 80, FontStyleBold, UnitPixel);
			graphics.DrawString(L"ゲームオーバー", -1, &bigFont, PointF(450, 200), &redBrush);

			// Failed message
			SolidBrush whiteBrush(Color(255, 255, 255, 255));
			Font medFont(&fontFamily, 48, FontStyleBold, UnitPixel);
			graphics.DrawString(L"パズルを解くことができませんでした", -1, &medFont, PointF(300, 350), &whiteBrush);

			// Stats
			Font smallFont(&fontFamily, 36, FontStyleRegular, UnitPixel);
			wchar_t statsText[100];
			swprintf_s(statsText, L"クリック数: %d回", clickCount);
			graphics.DrawString(statsText, -1, &smallFont, PointF(550, 450), &whiteBrush);

			// Return to menu button
			SolidBrush buttonBrush(Color(150, 100, 100, 100));
			graphics.FillRectangle(&buttonBrush, 600, 600, 400, 80);
			Font buttonFont(&fontFamily, 32, FontStyleBold, UnitPixel);
			graphics.DrawString(L"メニューに戻る", -1, &buttonFont, PointF(650, 620), &whiteBrush);

			if (isLeftClicked && !isLeftClickProcessed &&
				p.x >= 600 && p.x <= 1000 && p.y >= 600 && p.y <= 680) {
				currentState = Menu;
				levelInitialized = false;
				isLeftClickProcessed = true;
			}

		} break;
		}

#pragma region CleanUp & Double Buffer copy
		BitBlt(hdc, 0, 0, WindowWidth, WindowHeight, memDC, 0, 0, SRCCOPY);
		SelectObject(memDC, oldBmp);
		DeleteObject(memBmp);
		DeleteDC(memDC);
		ReleaseDC(hWnd, hdc);
#pragma endregion
	}
};

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd) {
#pragma region Windows Settings
	GdiplusStartupInput gdiplusStartupInput;
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
	WNDCLASSEX wc;
	HWND hWnd;
	MSG msg;

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wc.lpfnWndProc = (WNDPROC)WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, MAKEINTRESOURCE(IDI_LOGO));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = TEXT("ModelApp");
	wc.hIconSm = LoadIcon(NULL, MAKEINTRESOURCE(IDI_LOGO));

	RegisterClassEx(&wc);

	hWnd = CreateWindow(wc.lpszClassName,
		TEXT("ファン・ゴッホの人生 - Van Gogh Puzzle"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		WindowWidth,
		WindowHeight,
		NULL,
		NULL,
		hInstance,
		NULL
	);
	ShowWindow(hWnd, nShowCmd);
	UpdateWindow(hWnd);
#pragma endregion

	Game game;
	isRunning = true;
	game.Start(hInstance);

	while (isRunning)
	{
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				isRunning = false;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		game.HandleInput(hWnd);
		game.Render(hWnd);
		Sleep(16);  // ~60 FPS
	}
	GdiplusShutdown(gdiplusToken);
	return int(msg.wParam);
}