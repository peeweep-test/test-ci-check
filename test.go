package main

import (
    "errors"
    "fmt"
    "math"
)

func circleArea(radius float64) (float64, error) {
    if radius < 0 { 		// 如果小于0则运行以下
        return 0, errors.New("Area calculation failed, radius is less than zero")
    }
    return math.Pi * radius * radius, nil
}

func main() {
    radius := -10.00
    area, err := circleArea(radius)
    if err != nil { 		// 如果为空
        fmt.Println(err)
        return
    }
    fmt.Printf("Area of circle %0.2f", area)
}

// Area calculation failed, radius is less than zero
