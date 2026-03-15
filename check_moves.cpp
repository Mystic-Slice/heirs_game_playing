// check_moves.cpp — Count legal moves for the current player.
// Reads input.txt from cwd, prints move count to stdout.

#include <array>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

constexpr int BOARD_SIZE = 12;
constexpr char EMPTY = '.';
constexpr std::array<char, BOARD_SIZE> kCols = {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'j', 'k', 'm', 'n'};

struct Move { int sr, sc, dr, dc; char captured; };

bool IsFriendly(char p, bool w) {
  if (p == EMPTY) return false;
  return w ? (std::isupper((unsigned char)p) != 0)
           : (std::islower((unsigned char)p) != 0);
}
bool InBounds(int r, int c) {
  return r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE;
}

static const int kDiagDirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
static const int kOrthDirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
static const int kAllDirs[8][2] = {{-1,-1},{-1,0},{-1,1},{0,-1},
                                    {0,1},{1,-1},{1,0},{1,1}};

void TryAdd(int sr,int sc,int dr,int dc,bool w,const char b[BOARD_SIZE][BOARD_SIZE],std::vector<Move>*m){
  if(!InBounds(dr,dc))return;
  char t=b[dr][dc]; if(IsFriendly(t,w))return;
  m->push_back({sr,sc,dr,dc,t});
}
void AddSliding(int sr,int sc,bool w,int ms,const int d[][2],int dc2,const char b[BOARD_SIZE][BOARD_SIZE],std::vector<Move>*m){
  for(int i=0;i<dc2;++i)for(int s=1;s<=ms;++s){
    int nr=sr+d[i][0]*s,nc=sc+d[i][1]*s;
    if(!InBounds(nr,nc))break; char t=b[nr][nc];
    if(IsFriendly(t,w))break; m->push_back({sr,sc,nr,nc,t});
    if(t!=EMPTY)break;
  }
}
bool HasAdjFriendly(int sr,int sc,int dr,int dc,bool w,const char b[BOARD_SIZE][BOARD_SIZE]){
  for(int rr=-1;rr<=1;++rr)for(int cc=-1;cc<=1;++cc){
    if(!rr&&!cc)continue; int nr=dr+rr,nc=dc+cc;
    if(!InBounds(nr,nc))continue; if(nr==sr&&nc==sc)continue;
    if(IsFriendly(b[nr][nc],w))return true;
  } return false;
}
void GenBaby(int r,int c,bool w,const char b[BOARD_SIZE][BOARD_SIZE],std::vector<Move>*m){
  int dir=w?-1:1;
  for(int s=1;s<=2;++s){int nr=r+dir*s;if(!InBounds(nr,c))break;
    if(s==2&&b[r+dir][c]!=EMPTY)break; char t=b[nr][c];
    if(IsFriendly(t,w))break; m->push_back({r,c,nr,c,t}); if(t!=EMPTY)break;}
}
void GenScout(int r,int c,bool w,const char b[BOARD_SIZE][BOARD_SIZE],std::vector<Move>*m){
  int dir=w?-1:1;
  for(int f=1;f<=3;++f){int nr=r+dir*f;if(!InBounds(nr,c))break;
    for(int s=-1;s<=1;++s){int nc=c+s;if(!InBounds(nr,nc))continue;
      char t=b[nr][nc];if(IsFriendly(t,w))continue;m->push_back({r,c,nr,nc,t});}}
}
void GenSibling(int r,int c,bool w,const char b[BOARD_SIZE][BOARD_SIZE],std::vector<Move>*m){
  for(auto&d:kAllDirs){int nr=r+d[0],nc=c+d[1];if(!InBounds(nr,nc))continue;
    char t=b[nr][nc];if(IsFriendly(t,w))continue;
    if(HasAdjFriendly(r,c,nr,nc,w,b))m->push_back({r,c,nr,nc,t});}
}

int main(){
  std::ifstream in("input.txt"); if(!in){std::cout<<0;return 1;}
  std::string color; if(!(in>>color)){std::cout<<0;return 1;}
  bool w=(color=="WHITE"); double t1,t2; in>>t1>>t2;
  char board[BOARD_SIZE][BOARD_SIZE]; std::string line;
  for(int r=0;r<BOARD_SIZE;++r){if(!(in>>line)||(int)line.size()!=BOARD_SIZE){std::cout<<0;return 1;}
    for(int c=0;c<BOARD_SIZE;++c)board[r][c]=line[c];}

  std::vector<Move> moves; moves.reserve(192);
  for(int r=0;r<BOARD_SIZE;++r)for(int c=0;c<BOARD_SIZE;++c){
    char p=board[r][c]; if(p==EMPTY||!IsFriendly(p,w))continue;
    char pu=(char)std::toupper((unsigned char)p);
    switch(pu){
      case 'B':GenBaby(r,c,w,board,&moves);break;
      case 'P':for(auto&d:kAllDirs)TryAdd(r,c,r+d[0],c+d[1],w,board,&moves);break;
      case 'X':AddSliding(r,c,w,3,kAllDirs,8,board,&moves);break;
      case 'Y':for(auto&d:kDiagDirs)TryAdd(r,c,r+d[0],c+d[1],w,board,&moves);break;
      case 'G':AddSliding(r,c,w,2,kOrthDirs,4,board,&moves);break;
      case 'T':AddSliding(r,c,w,2,kDiagDirs,4,board,&moves);break;
      case 'S':GenScout(r,c,w,board,&moves);break;
      case 'N':GenSibling(r,c,w,board,&moves);break;
    }
  }
  std::cout<<moves.size();
  return 0;
}
