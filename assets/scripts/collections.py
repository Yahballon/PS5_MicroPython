print("Collections demo")
squares = [x * x for x in range(10)]
print("Squares:", squares)
d = {"console": "PS5", "engine": "MicroPython"}
for key in d:
    print(key, "=", d[key])
try:
    1 // 0
except ZeroDivisionError:
    print("Caught division by zero")
