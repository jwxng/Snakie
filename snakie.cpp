// compile with: clang++ -std=c++20 -Wall -Werror -Wextra -Wpedantic -g3 -o snakie snakie.cpp
// run with: ./snakie 2> /dev/null
// run with: ./snakie 2> debugoutput.txt
//  "2>" redirect standard error (STDERR; cerr)
//  /dev/null is a "virtual file" which discard contents

// Works best in Visual Studio Code if you set:
//   Settings -> Features -> Terminal -> Local Echo Latency Threshold = -1

// https://en.wikipedia.org/wiki/ANSI_escape_code#3-bit_and_4-bit

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono> // for dealing with time intervals
#include <cmath> // for max() and min()
#include <termios.h> // to control terminal modes
#include <unistd.h> // for read()
#include <fcntl.h> // to enable / disable non-blocking read()

// Because we are only using #includes from the standard, names shouldn't conflict
using namespace std;

// Constants

// Disable JUST this warning (in case students choose not to use some of these constants)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"

const char NULL_CHAR     { 'z' };
const char UP_CHAR       { 'w' };
const char DOWN_CHAR     { 's' };
const char LEFT_CHAR     { 'a' };
const char RIGHT_CHAR    { 'd' };
const char QUIT_CHAR     { 'q' };
const char FREEZE_CHAR   { 'f' };
const char CLEAR_CHAR    { 'c' };
const char BLOCKING_CHAR { 'b' };
const char COMMAND_CHAR  { 'o' };
const char EXIT_CHAR     { 'e' };

const string ANSI_START { "\033[" };
const string START_COLOUR_PREFIX {"1;"};
const string START_COLOUR_SUFFIX {"m"};
const string STOP_COLOUR  {"\033[0m"};

const unsigned int COLOUR_IGNORE  { 0 }; // this is a little dangerous but should work out OK
const unsigned int COLOUR_BLACK   { 30 };
const unsigned int COLOUR_RED     { 31 };
const unsigned int COLOUR_GREEN   { 32 };
const unsigned int COLOUR_YELLOW  { 33 };
const unsigned int COLOUR_BLUE    { 34 };
const unsigned int COLOUR_MAGENTA { 35 };
const unsigned int COLOUR_CYAN    { 36 };
const unsigned int COLOUR_WHITE   { 37 };

const unsigned short MOVING_NOWHERE { 0 };
const unsigned short MOVING_LEFT    { 1 };
const unsigned short MOVING_RIGHT   { 2 };
const unsigned short MOVING_UP      { 3 };
const unsigned short MOVING_DOWN    { 4 };

//size of playing screen
const int numRow = 20;
const int numCol = 40;


#pragma clang diagnostic pop

// Types

bool gameOver;
unsigned int score;
char prevChar;

// Using signed and not unsigned to avoid having to check for ( 0 - 1 ) being very large
struct position { int row; int col; };

position scoreDisplay {numRow+1,0};

struct snakie
{
    position pos {1,1};
    unsigned int colour = COLOUR_GREEN;
    float speed = 1.0;
};

struct fruitie {
    position pos {0,0};
    unsigned int color = COLOUR_MAGENTA;
};

struct tail {

    position pos {0,0};

    vector <position> tailVect{pos};

    position previous {0,0};
    position secondPrevious {0,0};

    unsigned int tailLength = 1;

    unsigned int color = COLOUR_GREEN;

};

// Globals

struct termios initialTerm;
default_random_engine generator;
uniform_int_distribution<int> movementX(2, 20);
uniform_int_distribution<int> movementY(2, 40);
uniform_int_distribution<unsigned int> snakeColour( COLOUR_RED, COLOUR_WHITE );

// Utilty Functions

// These two functions are taken from StackExchange and are 
// all of the "magic" in this code.
auto SetupScreenAndInput() -> void
{
    struct termios newTerm;
    // Load the current terminal attributes for STDIN and store them in a global
    tcgetattr(fileno(stdin), &initialTerm);
    newTerm = initialTerm;
    // Mask out terminal echo and enable "noncanonical mode"
    // " ... input is available immediately (without the user having to type 
    // a line-delimiter character), no input processing is performed ..."
    newTerm.c_lflag &= ~ICANON;
    newTerm.c_lflag &= ~ECHO;
    newTerm.c_cc[VMIN] = 1;
 
    // Set the terminal attributes for STDIN immediately
    auto result { tcsetattr(fileno(stdin), TCSANOW, &newTerm) };
    if ( result < 0 ) { cerr << "Error setting terminal attributes [" << result << "]" << endl; }

}
auto TeardownScreenAndInput() -> void
{
    // Reset STDIO to its original settings
    tcsetattr( fileno( stdin ), TCSANOW, &initialTerm );
    //system("clear");
}
auto SetNonblockingReadState( bool desiredState = true ) -> void
{
    auto currentFlags { fcntl( 0, F_GETFL ) };
    if ( desiredState ) { fcntl( 0, F_SETFL, ( currentFlags | O_NONBLOCK ) ); }
    else { fcntl( 0, F_SETFL, ( currentFlags & ( ~O_NONBLOCK ) ) ); }
    cerr << "SetNonblockingReadState [" << desiredState << "]" << endl;
}

auto MoveTo( unsigned int x, unsigned int t ) -> void { cout << ANSI_START << x << ";" << t << "H" << flush; }
// Everything from here on is based on ANSI codes
// Note the use of "flush" after every write to ensure the screen updates

auto MakeColour( string inputString, 
                 const unsigned int foregroundColour = COLOUR_WHITE,
                 const unsigned int backgroundColour = COLOUR_IGNORE ) -> string
{
    string outputString;
    outputString += ANSI_START;
    outputString += START_COLOUR_PREFIX;
    outputString += to_string( foregroundColour );
    if ( backgroundColour ) 
    { 
        outputString += ";";
        outputString += to_string( ( backgroundColour + 10 ) ); // Tacky but works
    }
    outputString += START_COLOUR_SUFFIX;
    outputString += inputString;
    outputString += STOP_COLOUR;
    return outputString;
}

//Clears screen and displays borders
auto ClearScreen() -> void { 
    cout << ANSI_START << "2J" << flush; 

    for (int i = 0; i <= numCol;i++){
        
        for (int j = 0; j <= numRow;j++){
            if (i == 0 || i == numCol){
                MoveTo(j,i);
                cout << MakeColour("#", COLOUR_WHITE) << flush;
            }
            if (j == 0 || j == numRow){
                MoveTo(j,i);
                cout <<  MakeColour("#", COLOUR_WHITE) << flush;
            }
        }
        }
    }

auto GenerateFruit(fruitie & fruit) -> void {
    fruit.pos = { movementX(generator), movementY(generator)}; //uses random generator for coordinates
}

//Display when player dies
auto GameOverDisplay() -> void{
    
    position gameOver {numRow/2, numCol/2 -3}; //middle of screen and 3 to the left so that "GAMEOVER" is centered around letter "E"
    
    cout << ANSI_START << "2J" << flush; 

    //Displays borders
    for (int i = 0; i <= numCol;i++){
        
        for (int j = 0; j <= numRow;j++){
            if (i == 0 || i == numCol){
                MoveTo(j,i);
                cout << MakeColour("#", COLOUR_WHITE) << flush;
            }
            if (j == 0 || j == numRow){
                MoveTo(j,i);
                cout <<  MakeColour("#", COLOUR_WHITE) << flush;
            }
        }
    }

    MoveTo(gameOver.row, gameOver.col);
    cout << MakeColour("GAMEOVER", COLOUR_RED) << endl;

    MoveTo(gameOver.row +2, (gameOver.col));
    cout << MakeColour("Score: " + to_string(score), COLOUR_CYAN) << endl;

    MoveTo(numRow+1, 0);
    cout << "Press 'e' to exit" << endl;

}

auto HideCursor() -> void { cout << ANSI_START << "?25l" << flush; }
auto ShowCursor() -> void { cout << ANSI_START << "?25h" << flush; }
auto GetTerminalSize() -> position
{
    // This feels sketchy but is actually about the only way to make this work
    MoveTo(999,999);
    cout << ANSI_START << "6n" << flush ;
    string responseString;
    char currentChar { static_cast<char>( getchar() ) };
    while ( currentChar != 'R')
    {
        responseString += currentChar;
        currentChar = getchar();
    }
    // format is ESC[nnn;mmm ... so remove the first 2 characters + split on ; + convert to unsigned int
    // cerr << responseString << endl;
    responseString.erase(0,2);
    // cerr << responseString << endl;
    auto semicolonLocation = responseString.find(";");
    // cerr << "[" << semicolonLocation << "]" << endl;
    auto rowsString { responseString.substr( 0, semicolonLocation ) };
    auto colsString { responseString.substr( ( semicolonLocation + 1 ), responseString.size() ) };
    // cerr << "[" << rowsString << "][" << colsString << "]" << endl;
    auto rows = stoul( rowsString );
    auto cols = stoul( colsString );
    position returnSize { static_cast<int>(rows), static_cast<int>(cols) };
    // cerr << "[" << returnSize.row << "," << returnSize.col << "]" << endl;
    return returnSize;
}

// Snake Logic
// updates tail position - increments starting at index 1 because index 0 is head position
auto UpdateTailPosition(tail snakeTail) -> tail {
    for (unsigned int i = 1; i < snakeTail.tailLength; i++)
    {
        //each position index will take position of previous index using temp variable 'secondPrevious'
        snakeTail.secondPrevious = snakeTail.tailVect[i]; 
        snakeTail.tailVect[i] = snakeTail.previous; //need index value here
        snakeTail.previous = snakeTail.secondPrevious;
        
    }
    return snakeTail;
}

//checks if snake head == any snake tail value 
//will handle gameover at boundaries since tail will collapse into head (reference max/min function)
//UpdatePositions will require 1 more iteration to check gameover at boundaries - room for improvement here
auto CheckGameOver(snakie s, tail snakeTail) -> void {
    for (unsigned int i = 1; i < snakeTail.tailLength; i++){
        if (snakeTail.tailVect[i].row == s.pos.row && snakeTail.tailVect[i].col == s.pos.col){
            gameOver = true;
        }
    }
}

auto UpdatePositions( snakie & head, fruitie & fruit, tail & snakeTail, char currentChar ) -> void
{
    // Deal with movement commands
    int commandRowChange = 0; 
    int commandColChange = 0;

    
    if ( currentChar == UP_CHAR ) { commandRowChange -= 1; prevChar = UP_CHAR;}
    else if ( currentChar == DOWN_CHAR )  { commandRowChange += 1; prevChar = DOWN_CHAR;}
    else if ( currentChar == LEFT_CHAR )  { commandColChange -= 1; prevChar = LEFT_CHAR;}
    else if ( currentChar == RIGHT_CHAR ) { commandColChange += 1; prevChar = RIGHT_CHAR;}
    
    //continues moving tail even though no key is pressed
    else{
        if (prevChar == UP_CHAR){
            commandRowChange -= 1;
        }
        if (prevChar == DOWN_CHAR){
            commandRowChange += 1;
        }
        if (prevChar == LEFT_CHAR){
            commandColChange -= 1;
        }
        if (prevChar == RIGHT_CHAR){
            commandColChange += 1;
        }
    }

    // Update the position of each snake
    // Use a reference so that the actual position updates
    
        auto currentRowChange { commandRowChange };
        auto currentColChange { commandColChange };
      
        auto proposedRow { head.pos.row + currentRowChange };
        auto proposedCol { head.pos.col + currentColChange };

        //Get previous position of head
        snakeTail.previous = snakeTail.tailVect[0];

        //update position of head 
        head.pos.row = max(  2, min( numRow-1, proposedRow ) );
        head.pos.col = max(  2, min( numCol-1, proposedCol ) );

        //check if head landed on fruit
        if (head.pos.row == fruit.pos.row && head.pos.col == fruit.pos.col){
            
            GenerateFruit(fruit);

            score++;

            snakeTail.tailLength++;
            snakeTail.tailVect.push_back({0,0}); 
        }

        //Update position of tail 
        snakeTail = UpdateTailPosition(snakeTail);

        //Make sure first element of tail is head for next iteration 
        //otherwise left with index 0 and index 1 of tailVect as the same
        snakeTail.tailVect[0] = head.pos;

        CheckGameOver(head, snakeTail);
}

auto DrawSnakie(fruitie & fruit, tail & snakeTail) -> void
{
    for (auto positionInTail : snakeTail.tailVect){
        MoveTo(positionInTail.row, positionInTail.col);
        cout << MakeColour("O", snakeTail.color) << flush;
    }

    MoveTo(fruit.pos.row, fruit.pos.col);
    cout << MakeColour("F", fruit.color) << flush;

    MoveTo(scoreDisplay.row, scoreDisplay.col); 
    cout << "Score: " << score << endl;
}

auto main() -> int
{
    // Set Up the system to receive input
    SetupScreenAndInput();

    // Check that the terminal size is large enough for our snakie
    const position TERMINAL_SIZE { GetTerminalSize() };
    if ( ( TERMINAL_SIZE.row < 20 ) or ( TERMINAL_SIZE.col < 40 ) )
    {
        ShowCursor();
        TeardownScreenAndInput();
        cout << endl <<  "Terminal window must be at least 30 by 50 to run this game" << endl;
        return EXIT_FAILURE;
    }

    // State Variables

    unsigned int ticks {0};

    char currentChar { CLEAR_CHAR }; // the first act will be to create a snake
    string currentCommand;

    bool allowBackgroundProcessing { true };
    bool showCommandline { false };

    auto startTimestamp { chrono::steady_clock::now() };
    auto endTimestamp { startTimestamp };
    int elapsedTimePerTick { 100 }; // Every 0.1s check on things
    
    SetNonblockingReadState( allowBackgroundProcessing );
    ClearScreen();
    HideCursor();

    //Magic Ends Here
    gameOver = false;
    score = 0;
    
    fruitie fruit {
        .pos = {movementX(generator), movementY(generator) }
    };
    
    snakie snakeHead { 
        .pos = { .row = 1, .col = 1 } ,
        .colour = COLOUR_GREEN,
        .speed = 1.0
    };

    tail snakeTail{};

    while( (currentChar != QUIT_CHAR) && (gameOver == false) )
    {
        endTimestamp = chrono::steady_clock::now();
        auto elapsed { chrono::duration_cast<chrono::milliseconds>( endTimestamp - startTimestamp ).count() };
        // We want to process input and update the world when EITHER  
        // (a) there is background processing and enough time has elapsed
        // (b) when we are not allowing background processing.
        if ( 
                 ( allowBackgroundProcessing and ( elapsed >= elapsedTimePerTick ) )
              or ( not allowBackgroundProcessing ) 
           )
        {
            ticks++;
            cerr << "Ticks [" << ticks << "] allowBackgroundProcessing ["<< allowBackgroundProcessing << "] elapsed [" << elapsed << "] currentChar [" << currentChar << "] currentCommand [" << currentCommand << "]" << endl;
            if ( currentChar == BLOCKING_CHAR ) // Toggle background processing
            {
                allowBackgroundProcessing = not allowBackgroundProcessing;
                SetNonblockingReadState( allowBackgroundProcessing );
            }
            if ( currentChar == COMMAND_CHAR ) // Switch into command line mode
            {
                allowBackgroundProcessing = false;
                SetNonblockingReadState( allowBackgroundProcessing ); 
                showCommandline = true;
            }
            if ( currentCommand.compare( "resume" ) == 0 ) { cerr << "Turning off command line" << endl; showCommandline = false; }
           // if ( ( currentChar == FREEZE_CHAR ) or ( currentCommand.compare( "freeze" ) == 0 ) ) { ToggleFrozensnake( snakie ); } 
            //if ( ( currentChar == CREATE_CHAR ) or ( currentCommand.compare( "create" ) == 0 ) ) { Createsnakeie( snakie ); } 

            UpdatePositions( snakeHead, fruit, snakeTail, currentChar );
            ClearScreen();
            
            DrawSnakie(fruit, snakeTail);

            if ( showCommandline )
            {
                cerr << "Showing Command Line" << endl;
                MoveTo( 21, 1 ); 
                ShowCursor();
                cout << "Command:" << flush;
            }
            else { HideCursor(); }

            // Clear inputs in preparation for the next iteration
            startTimestamp = endTimestamp;    
            currentChar = NULL_CHAR;
            currentCommand.clear();
        }
        // Depending on the blocking mode, either read in one character or a string (character by character)
        if ( showCommandline )
        {
            while ( read( 0, &currentChar, 1 ) == 1 && ( currentChar != '\n' ) )
            {
                cout << currentChar << flush; // the flush is important since we are in non-echoing mode
                currentCommand += currentChar;
            }
            cerr << "Received command [" << currentCommand << "]" << endl;
            currentChar = NULL_CHAR;
        }
        else
        {
            read( 0, &currentChar, 1 );
        }
    }
    // Tidy Up and Close Down
    ShowCursor();
    SetNonblockingReadState( false );
    
    //GameOverDisplay();

    //if key is pressed then --> call function below
    //waits until "e" is pressed to exit and tear down input

    while ((currentChar != EXIT_CHAR)){
        GameOverDisplay();

        if ( showCommandline )
        {
            while ( read( 0, &currentChar, 1 ) == 1 && ( currentChar != '\n' ) )
            {
                cout << currentChar << flush; // the flush is important since we are in non-echoing mode
                currentCommand += currentChar;
            }
            cerr << "Received command [" << currentCommand << "]" << endl;
            currentChar = NULL_CHAR;
        }
        else
        {
            read( 0, &currentChar, 1 );
        }
    }
    TeardownScreenAndInput();
    

    cout << endl; // be nice to the next command
    return EXIT_SUCCESS;
}
