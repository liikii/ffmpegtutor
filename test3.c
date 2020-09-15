/*
gcc -o test3 test3.c `sdl-config --cflags --libs`
scp test3.c liikii@192.168.1.104:/home/liikii/tmp3/
*/
#include <SDL.h>
#include <stdio.h>
#include <unistd.h>
#include <SDL_thread.h>

SDL_bool condition = SDL_FALSE;
static SDL_mutex *lock;
SDL_cond *cond;



// void thread1(){
//     SDL_LockMutex(lock);
//     while (!condition) {
//         printf("                                         <============等待\n");
//         SDL_CondWait(cond, lock);
//         printf("                       <===========等到了~_~!!!\n");
//     }
//     printf("                                          <===========end\n");
//     SDL_UnlockMutex(lock);
// }
void thread2(){
    int a = SDL_LockMutex(lock);
    printf("                                          thread2  %d\n", a);
    printf("                                          <============thread2等待\n");
    // This function unlocks the specified mutex and waits for another thread to call SDL_CondSignal() or SDL_CondBroadcast() on the condition variable cond. Once the condition variable is signaled, the mutex is re-locked and the function returns.
    int a2 = SDL_CondWait(cond, lock);
    printf("                                          a2 = %d\n", a2);
    printf("                                          <===========thread2接收到通知,继续执行~_~!!!\n");
    SDL_UnlockMutex(lock);
    printf("                                          <===========thread2 end\n");
}

int main(){
    lock = SDL_CreateMutex();
    cond = SDL_CreateCond();
    SDL_Thread * t = SDL_CreateThread(thread2, NULL);
    if(!t){
        printf("  %s",SDL_GetError);
        return -1;
    }
    sleep(2);
    printf("main执行中=====>\n");
    printf("main执行中=====>\n");

    int f = SDL_LockMutex(lock);
    printf("main lock flag : %d\n", f);
    printf("main执行后准备发送signal====================>\n");
    condition = SDL_TRUE;
    SDL_CondSignal(cond);
    SDL_UnlockMutex(lock);
    printf("sleep 3\n");
    sleep(2);

}