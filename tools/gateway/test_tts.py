import asyncio, time, edge_tts

async def test():
    sentences = ['Câu thứ nhất kiểm tra kết nối.', 'Câu thứ hai xem tốc độ thế nào.', 'Câu thứ ba kiểm tra hoàn tất.']
    for s in sentences:
        t0 = time.time()
        comm = edge_tts.Communicate(s, 'vi-VN-HoaiMyNeural')
        data = bytearray()
        async for chunk in comm.stream():
            if chunk['type'] == 'audio':
                data.extend(chunk['data'])
        print(f'Sentence "{s}": {len(data)} bytes in {time.time()-t0:.2f}s')

if __name__ == "__main__":
    asyncio.run(test())
