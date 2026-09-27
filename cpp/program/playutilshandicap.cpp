#include "../program/playutils.h"

// Kept separately from playutils.cpp because it only depends on the board,
// so it can be linked into lightweight targets (such as katago_tests) without the search.

using namespace std;

vector<Loc> PlayUtils::generateFixedHandicap(const Board& board, const int n) {
  const int xSize = board.x_size;
  const int ySize = board.y_size;
  if(xSize < 7 || ySize < 7)
    throw StringError("Board is too small for fixed handicap");
  if((xSize % 2 == 0 || ySize % 2 == 0) && n > 4)
    throw StringError("Fixed handicap > 4 is not allowed on boards with even dimensions");
  if((xSize <= 7 || ySize <= 7) && n > 4)
    throw StringError("Fixed handicap > 4 is not allowed on boards with size 7");
  if(n < 2)
    throw StringError("Fixed handicap < 2 is not allowed");
  if(n > 9)
    throw StringError("Fixed handicap > 9 is not allowed");

  int xCoords[3]; //Corner, corner, side
  int yCoords[3]; //Corner, corner, side
  if(xSize <= 12) { xCoords[0] = 2; xCoords[1] = xSize-3; xCoords[2] = xSize/2; }
  else            { xCoords[0] = 3; xCoords[1] = xSize-4; xCoords[2] = xSize/2; }
  if(ySize <= 12) { yCoords[0] = 2; yCoords[1] = ySize-3; yCoords[2] = ySize/2; }
  else            { yCoords[0] = 3; yCoords[1] = ySize-4; yCoords[2] = ySize/2; }

  vector<Loc> locs;

  auto s = [&](const int xi, const int yi) {
    locs.push_back(Location::getLoc(xCoords[xi],yCoords[yi],board.x_size));
  };
  if(n == 2) { s(0,1); s(1,0); }
  else if(n == 3) { s(0,1); s(1,0); s(0,0); }
  else if(n == 4) { s(0,1); s(1,0); s(0,0); s(1,1); }
  else if(n == 5) { s(0,1); s(1,0); s(0,0); s(1,1); s(2,2); }
  else if(n == 6) { s(0,1); s(1,0); s(0,0); s(1,1); s(0,2); s(1,2); }
  else if(n == 7) { s(0,1); s(1,0); s(0,0); s(1,1); s(0,2); s(1,2); s(2,2); }
  else if(n == 8) { s(0,1); s(1,0); s(0,0); s(1,1); s(0,2); s(1,2); s(2,0); s(2,1); }
  else if(n == 9) { s(0,1); s(1,0); s(0,0); s(1,1); s(0,2); s(1,2); s(2,0); s(2,1); s(2,2); }
  else { ASSERT_UNREACHABLE; }

  return locs;
}
