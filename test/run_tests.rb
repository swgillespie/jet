require 'test/unit'

TEST_COMMAND = ENV["JET_TEST_EXE"] || "jet"

SchemeTest = Struct.new(:name, :filename, :output)

def create_test_cases(folder)
    tests = []
    Dir.glob(File.join folder, '*.jet').each do |file|
        tests << create_test_case(file)
    end

    return tests
end

def create_test_case(file)
    output_lines = []
    name = File.basename(file, '.jet')
    File.open file, "r" do |file_object|
        file_object.each_line do |line|
            line_output = /;OUTPUT: (?<output>.*)/.match(line)
            if line_output
                output_lines << line_output["output"]
            end
        end
    end

    return SchemeTest.new(name, file, output_lines)
end

def create_test_class(dir)
    Class.new Test::Unit::TestCase do
        create_test_cases(dir).each do |test|
            define_method "test_" + test.name do
                stdout = `#{TEST_COMMAND} #{test.filename} 2>&1`.lines
                assert_equal test.output.count, stdout.count, "wrong number of output lines"
                test.output.zip(stdout).each do |expected_line, actual_line|
                    assert_include expected_line, actual_line
                end
            end
        end
    end
end

module Test::Unit::Assertions
  def assert_include(expected_line, actual_line)
    assert actual_line.include?(expected_line), "Expected the line #{actual_line} to include #{expected_line}"
  end
end

Atoms = create_test_class "atoms"
Let = create_test_class "let"
Functions = create_test_class "functions"
Forms = create_test_class "forms"