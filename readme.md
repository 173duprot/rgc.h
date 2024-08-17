## <rgc.h> Realtime Garbage collector
Simple, single-header, [epoch-based](https://aturon.github.io/blog/2015/08/27/epoch/), non-blocking garbage collector written in 67 lines of C11.

Here's how you could use this in an ECS-type implimentation. 

```c
#include "rgc.h"

typedef struct { int x, y; } Position;
typedef struct { int vx, vy; } Velocity;

void update(Position *p, Velocity *v) { p->x += v->vx; p->y += v->vy; }

int main() {
    epoch_gc_t gc; init_gc(&gc);
    Position *p = malloc(sizeof(Position)); p->x = 0; p->y = 0;
    Velocity *v = malloc(sizeof(Velocity)); v->vx = 1; v->vy = 1;

    enter_critical(&gc, 0); update(p, v); exit_critical(&gc, 0);

    add_garbage(&gc, (node_t*)p); add_garbage(&gc, (node_t*)v); collect_garbage(&gc);

    return 0;
}
```
