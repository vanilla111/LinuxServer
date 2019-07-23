#ifndef TIME_WHEEL_TIMER
#define TIME_WHEEL_TIMER

#include <time.h>
#include <netinet/in.h>
#include <stdio.h>

#define BUFFER_SIZE 64
class tw_time;
struct client_data
{
	sockaddr_in address;
	int sockfd;
	char buf[BUFFER_SIZE];
	tw_timer* timer;
};

class tw_timer
{
public:
	tw_timer(int rot, int ts): next(NULL), prev(NULL), rotation(rot), time_slot(ts){}

	int rotation;  /* 记录定时器在时间轮多少圈后生效 */
	int time_slot; /* 记录定时器在时间轮上哪个槽 */
	void (*cb_func)(client_data); /* 定时器回调函数 */
	client_data* user_data;

	tw_timer* next;
	tw_timer* prev;
};

class time_wheel
{
public:
	time_wheel(): cur_slot(0)
	{
		for (int i = 0; i < N; ++i)
		{
			slots[i] = NULL;
		}
	}
	~time_wheel()
	{
		for (int i = 0; i < N; ++i)
		{
			tw_timer* tmp = tmp->next;
			while(tmp)
			{
				slots[i] = tmp->next;
				delete tmp;
				tmp = slots[i];
			}
		}
	}

	/* 根据定时值timeout创建一个定时器，并把它插入合适的槽中 */
	tw_timer* add_timer(int timeout)
	{
		if (timeout < 0) NULL;
		int ticks = 0;
		/* 根据超时参数计算它将在时间轮转动多少个嘀嗒后被触发，并将该嘀嗒数存放在ticks变量中 */
		if (timeout < SI) ticks = 1; // 小于槽间隔SI，向上折合1
		else ticks = timeout / SI;   // 否则向下折合

		int rotation = ticks / N;               // 多少圈后被触发
		int ts = (cur_slot + (ticks % N)) % N;  // 应该插入那个槽中
		tw_timer* timer = new tw_timer(rotation, ts);
		if (!slots[ts])
		{
			printf("add timer, rotation is %d, ts is %d, cur_slot is %d\n", rotation, ts, cur_slot);
			slots[ts] = timer;
		}
		else
		{
			timer->next = slots[ts];
			slots[ts]->prev = timer;
			slots[ts] = timer;
		}
		return timer;
	}

	/* 删除目标定时器 */
	void del_timer(tw_timer* timer)
	{
		if (!timer) return;
		int ts = timer->time_slot;
		if (timer == slots[ts])
		{
			/* 如果是槽的头节点 */
			slots[ts] = slots[ts]->next;
			if (slots[ts]) slots[ts]->prev = NULL;
			delete timer;
		}
		else
		{
			timer->prev->next = timer->next;
			if (timer->next) timer->next->prev = timer->prev;
			delete timer;
		}
	}

	/* 时间间隔SI到之后，调用该函数，时间轮向前滚动一个槽的间隔 */
	void tick()
	{
		tw_timer* tmp = slots[cur_slot];
		printf("current slot is %d\n", cur_slot);
		while(tmp)
		{
			printf("tick the timer once\n");
			if (tmp->rotation > 0)
			{
				/* 这一轮还不到触发时间 */
				tmp->rotation--;
				tmp = tmp->next;
			}
			else
			{
				/* 否则，定时器以及到期，执行定时任务，然后删除 */
				tmp->cb_func(tmp->user_data);
				if (tmp == slots[cur_slot])
				{
					printf("delete header in cur_slot\n");
					slots[cur_slot] = tmp->next;
					delete tmp;
					if(slots[cur_slot]) slots[cur_slot]->prev = NULL;
					tmp = slots[cur_slot];
				}
				else
				{
					tmp->prev->next = tmp->next;
					if (tmp->next) tmp->next->prev = tmp->prev;
					tw_timer* tmp2 = tmp->next;
					delete tmp;
					tmp = tmp2;
				}
			}
		}
		cur_slot = ++cur_slot % N; // 更新当前时间轮的槽
	}

private:
	static const int N = 60; // 槽的总数
	static const int SI = 1; // 转动间隔 1 S
	tw_timer* slots[N];
	int cur_slot;            // 时间轮当前槽
}

#endif