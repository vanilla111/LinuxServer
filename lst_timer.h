#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#define BUFFER_SIZE 64
class util_timer;

struct client_data
{
	sockaddr_in address;
	int sockfd;
	char buf[BUFFER_SIZE];
	util_timer* timer;
};

class util_timer
{
public:
	util_timer():prev(NULL), next(NULL){}
public:
	time_t expire; /* 任务超时时间，绝对时间 */
	void(*cb_func)(client_data*); /* 回调函数 */
	client_data* user_data;
	util_timer* prev;
	util_timer* next;
};

/* 定时器链表，升序、双向、带有头节点和尾节点 */
/* 添加定时器O(n)，删除O(1)，执行O(1) */
class sort_timer_lst
{
public:
	sort_timer_lst():head(NULL), tail(NULL) {}
	~sort_timer_lst()
	{
		util_timer* tmp = head;
		while(tmp)
		{
			head = tmp->next;
			delete tmp;
			tmp = head;
		}
	}

	/* 添加一个定时器到链表中 */
	void add_timer(util_timer* timer)
	{
		if (!timer) return;
		if (!head)
		{
			head = tail = timer;
			return;
		}
		/* 将定时器插入到合适的位置 */
		if (timer->expire < head->expire)
		{
			/* 小于链表中所有，插入到头 */
			timer->next = head;
			head->prev = timer;
			head = timer;
			return;
		}
		/* 在链表中寻找合适的位置插入 */
		add_timer(timer, head);
	}

	/* 调整一个定时器在链表中的位置，这里只考虑了时间延长的情况，即向表尾调整 */
	void adjust_timer(util_timer* timer)
	{
		if (!timer) return;
		util_timer* tmp = timer->next;
		/* 如果是在表尾 或者 调整后也比后一个时间小 则直接返回 */
		if (!tmp || (timer->expire < tmp->expire)) return;
		/* 如果被调整的是头部，取出后重新插入 */
		if (timer == head)
		{
			head = head->next;
			head->prev = NULL;
			timer->next = NULL;
			add_timer(timer, head);
		}
		else
		{
			timer->prev->next = timer->next;
			timer->next->prev = timer->prev;
			add_timer(timer, timer->next);
		}
	}

	void del_timer(util_timer* timer)
	{
		if (!timer) return;
		/* 只有一个定时器 */
		if ((timer == head) && (timer == tail))
		{
			delete timer;
			head = NULL;
			tail = NULL;
			return;
		}
		if (timer == head)
		{
			head = head->next;
			head->prev = NULL;
			delete timer;
			return;
		}
		if (timer == tail)
		{
			tail = tail->prev;
			tail->next = NULL;
			delete timer;
			return;
		}
		timer->prev->next = timer->next;
		timer->next->prev = timer->prev;
		delete timer;
	}

	/* SIGALRM信号每次被触发就执行一次tick函数，用来处理链表上到期的任务 */
	void tick()
	{
		if (!head) return;
		printf("timer tick\n");
		time_t cut = time(NULL); //系统当前时间
		util_timer* tmp = head;
		/* 从头带尾依次处理每一个到期的定时器 */
		while (tmp)
		{
			if (cut < tmp->expire) break;
			tmp->cb_func(tmp->user_data);
			head = tmp->next;
			if (head) head->prev = NULL;
			delete tmp;
			tmp = head;
		}
	}

private:
	/* 重载辅助函数，用于在链表lst_head后插入一个新的节点 */
	void add_timer(util_timer* timer, util_timer* lst_head)
	{
		util_timer* prev = lst_head;
		util_timer* tmp = prev->next;
		while(tmp)
		{
			if (timer->expire < tmp->expire)
			{
				prev->next = timer;
				timer->next = tmp;
				timer->prev = prev;
				tmp->prev = tmp;
				break;
			}
			prev = tmp;
			tmp = tmp->next;
		}
		if (!tmp) // 如果插入的位置是末尾
		{
			prev->next = timer;
			timer->prev = prev;
			timer->next = NULL;
			tail = timer;
		}
	}

private:
	util_timer* head;
	util_timer* tail;
};

#endif