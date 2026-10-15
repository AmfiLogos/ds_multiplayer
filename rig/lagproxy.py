#!/usr/bin/env python3
"""TCP lag proxy for pair tests (lan_v10, review finding 4): listen_port -> 127.0.0.1:target_port, each direction
delayed by delay_ms +- jitter_ms per chunk (order kept), and loss_pct % of the chunks held back an extra 200-600 ms the way
a TCP retransmission after a lost packet does (TCP never loses data; loss shows up as a stall).

  lagproxy.py listen_port target_port delay_ms jitter_ms loss_pct"""
import asyncio, random, sys, time

LP, TP, DELAY, JIT, LOSS = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5])
rnd = random.Random(1234)
stats = {'chunks': 0, 'lost': 0, 'bytes': 0}


async def pipe(reader, writer, name):
    q = asyncio.Queue()
    last_due = [0.0]

    async def sender():
        while True:
            due, data = await q.get()
            if data is None:
                break
            d = due - time.monotonic()
            if d > 0:
                await asyncio.sleep(d)
            writer.write(data)
            await writer.drain()
        writer.close()

    task = asyncio.create_task(sender())
    while True:
        data = await reader.read(65536)
        if not data:
            await q.put((0, None)); break
        due = time.monotonic() + (DELAY + rnd.uniform(-JIT, JIT)) / 1000.0
        stats['chunks'] += 1; stats['bytes'] += len(data)
        if rnd.random() * 100 < LOSS:
            stats['lost'] += 1
            due += rnd.uniform(0.2, 0.6)                    # retransmission timeout
        due = max(due, last_due[0]); last_due[0] = due       # TCP keeps the order
        await q.put((due, data))
    await task


async def handle(r1, w1):
    r2, w2 = await asyncio.open_connection('127.0.0.1', TP)
    print(time.strftime('%H:%M:%S'), 'connection', flush=True)
    await asyncio.gather(pipe(r1, w2, 'up'), pipe(r2, w1, 'down'), return_exceptions=True)


async def report():
    while True:
        await asyncio.sleep(60)
        print(time.strftime('%H:%M:%S'), stats, flush=True)


async def main():
    srv = await asyncio.start_server(handle, '127.0.0.1', LP)
    print(time.strftime('%H:%M:%S'), f'lag proxy {LP} -> {TP}: {DELAY} +- {JIT} ms, {LOSS}% stalls', flush=True)
    asyncio.create_task(report())
    async with srv:
        await srv.serve_forever()

asyncio.run(main())
