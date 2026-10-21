print("Fibonacci sequence")
a, b = 0, 1
for i in range(20):
    print(i, ":", a)
    a, b = b, a + b
