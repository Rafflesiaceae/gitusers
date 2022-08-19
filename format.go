package main

func DigitsToSuperscript(str string) string {
	result := make([]rune, len(str))
	for _, c := range str {
		switch c {
		case '0':
			result = append(result, '⁰')
		case '1':
			result = append(result, '¹')
		case '2':
			result = append(result, '²')
		case '3':
			result = append(result, '³')
		case '4':
			result = append(result, '⁴')
		case '5':
			result = append(result, '⁵')
		case '6':
			result = append(result, '⁶')
		case '7':
			result = append(result, '⁷')
		case '8':
			result = append(result, '⁸')
		case '9':
			result = append(result, '⁹')
		default:
			result = append(result, c)
		}
	}
	return string(result)
}
