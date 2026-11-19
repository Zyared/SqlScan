package main

import (
	"bufio"
	_ "embed"
	"encoding/csv"
	"fmt"
	"io"
	"os"
	"slices"

	"github.com/corazawaf/coraza/v3"
)

//go:embed rules/REQUEST-901-INITIALIZATION.conf
var setupRules string

//go:embed rules/REQUEST-942-APPLICATION-ATTACK-SQLI.conf
var sqlRules string

// Classify one inert argument per CSV record using only local transactions.
func run(in io.Reader, out io.Writer) error {
	waf, err := coraza.NewWAF(coraza.NewWAFConfig().
		WithDirectives("SecRuleEngine DetectionOnly\nSecRequestBodyAccess On").
		WithDirectives(setupRules).
		WithDirectives(sqlRules))
	if err != nil {
		return err
	}
	input := csv.NewReader(in)
	input.FieldsPerRecord = 2
	if _, err := input.Read(); err != nil {
		return err
	}
	output := bufio.NewWriter(out)
	for {
		row, err := input.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
		tx := waf.NewTransaction()
		tx.ProcessURI("/review", "GET", "HTTP/1.1")
		tx.AddGetRequestArgument("sample", row[0])
		tx.ProcessRequestHeaders()
		_, err = tx.ProcessRequestBody()
		if err != nil {
			tx.Close()
			return err
		}
		detected := 0
		for _, match := range tx.MatchedRules() {
			id := match.Rule().ID()
			if id >= 942000 && id < 943000 && slices.Contains(match.Rule().Tags(), "attack-sqli") {
				detected = 1
				break
			}
		}
		tx.Close()
		if _, err = fmt.Fprintln(output, detected); err != nil {
			return err
		}
	}
	return output.Flush()
}

func main() {
	if err := run(os.Stdin, os.Stdout); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
